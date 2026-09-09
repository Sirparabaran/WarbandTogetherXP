# Flow: Battle Pipeline (dedicated battle end-to-end)

**Status:** AUDITED
**Validated against commit:** `ce0e287` (steamid persistence,
runtime-verified 2026-08-23: char dicts keyed by Steam acctid
(`coop_char_sid_<acctid>` via the `coop_char_store_dict_name` key-builders);
campaign result apply + Phase 2 XP moved out of the join handler into
`coop_player_hydrate` (triggered by ch49 identify ev 8 or a 5 s timeout);
battle-server char load deferred behind a hydration spawn gate (ch126
identify ev 54 / 5 s fallback); battle dict gains
`@battle_player_{i}_acctid` for reward keying. Hydration-affected rows
re-anchored @ `ce0e287`; other rows still carry `0b2500a` line numbers.
Prior stamp `9681486`: ev 14 freed by the packed char-sync; prior stamp
`0b2500a`: multiserver pass, runtime-verified 2026-07-18; prior stamp
`632466c` for the A9 row flip)

## Scope

The dedicated coop battle: a client on the campaign server requests a battle
from an encounter, the campaign server allocates a free slot from the battle
pool (up to `coop_battle_num_slots` = 4 dedicated battle servers), serializes
the encounter into that slot's `coop_battle_<slot>.wsedict` and announces it,
players B-key-join the assigned slot's battle server (port `7241 + 2*slot`),
fight one life, the battle server writes results back into the dict and
signals the campaign server over ENet IPC (slot-tagged), and rejoining
players get casualties + XP applied to their campaign parties. Entry point:
channel-49 `start_battle`/`request_siege` (client). Exit state: casualties
and XP applied, char dicts saved, `encounter_resolved` pushed to that
battle's participants, slot freed.
Local SP-style fights (events 16/17) share only the casualty core; they are
covered in `xp-sync.md`. Siege-specific mission behavior is in `siege.md`.

Module paths below are relative to `wse2work/Native-Coop-master/`; C paths
relative to repo root.

## Sequence diagram

```mermaid
sequenceDiagram
    participant C as Client (initiator)
    participant CS as Campaign Server (module + CoopWSEPlugin COOP_HOST)
    participant BS as Battle Server (module + CoopWSEPlugin COOP_BATTLE)

    Note over BS: at startup: persistent IPC connect to 127.0.0.1:7242,<br/>PKT_BATTLE_HELLO{slot} → campaign DLL maps peer→slot,<br/>publishes $g_coop_battle_slots_online bitmask
    C->>CS: ch49 start_battle (1) / request_siege (26)
    CS->>CS: coop_battle_launch_on_free_slot:<br/>coop_battle_find_free_slot (online mask ∧ not in progress;<br/>none → ch125 battle_rejected (44) param 0),<br/>coop_save_character (all players),<br/>coop_write_battle_data → dict_save coop_battle_{slot}<br/>(enemy parties marked slot_party_coop_battle_slot = slot+1)
    CS->>CS: $g_coop_battle_in_progress_{slot} = 1
    CS->>C: ch125 battle_available (10) to ALL players:<br/>(port 7241+2*slot, is_initiator, enemy party)
    Note over C: initiator: $g_coop_battle_connect_pending, deferred<br/>game-loop trigger connects; others: chooser table row,<br/>B key opens mnu_coop_join_battle
    C->>BS: join (port 7241 + 2*slot)
    BS->>BS: ti_server_player_joined: first join loads slot dict<br/>(coop_on_admin_panel_load), assigns campaign troop;<br/>char load deferred to coop_battle_player_hydrate<br/>(ch126 identify (54) or 5 s username fallback)
    BS->>BS: spawn gate (slot_player_spawned_this_round +<br/>hydration gate slot_player_join_time == 0),<br/>ti_on_agent_spawn: coop_equip_player_agent
    Note over BS: fight -- damage/4, one life,<br/>optional respawn-as-bot
    BS->>BS: coop_battle_check_round_end (A9: reserves,<br/>10s floor, 5s settle) → $g_round_ended=1
    BS->>BS: coop_copy_parties_to_file_mp → dict_save<br/>(casualties, battle_result, xp_rand, player strengths)<br/>then $coop_battle_started = -1
    BS->>BS: kick all players
    Note over BS: DLL poll thread sees -1 edge
    BS->>CS: ENet IPC (7242) PKT_BATTLE_END + battle_end_signal_t{slot,...}
    CS->>CS: coop_on_ipc_packet: $g_coop_battle_ended_{slot} = 1<br/>(also set on IPC peer loss for an in-progress slot → abort path)
    C->>CS: rejoin campaign (manual/browser)
    CS->>CS: multiplayer_campaign_player_joined: stamp hydration countdown,<br/>re-announce still-open (ended==0) slots via ev 10 (chooser rebuild)
    C->>CS: ch49 identify (8) — or 5 s timeout fallback
    CS->>CS: coop_player_hydrate: char load, then for EVERY slot with<br/>in_progress or ended set → coop_apply_battle_results<br/>(Phase 1, once per battle) + per-player Phase 2 XP from char dict
    CS->>C: ch125 encounter_resolved (9) — participants only —<br/>char/inv/party pushes; slot freed (+ ev 45 battle_slot_closed)
```

## Code anchors

Line numbers verified @ `0b2500a`.

| # | Step | File | Line | Symbol |
|---|------|------|------|--------|
| 1 | Client battle request arm (ch49 ev 1) | `module_coop_scripts.py` | 10139 | `multiplayer_campaign_client_events` (def `:10125`) → `coop_ev_cli_*` |
| 2 | Siege request arm (ch49 ev 26) | `module_coop_scripts.py` | 10142 | same dispatcher |
| 3 | Shared launch tail: slot alloc + save-all + dict write + mark + announce | `module_coop_scripts.py` | 9234–9290 | `coop_battle_launch_on_free_slot` (field + siege callers `:9300`, `:9324`) |
| 4 | — free-slot pick (online mask ∧ not in progress; −1 = pool full → ch125 ev 44 param 0) | `module_coop_scripts.py` | 10465–10485 | `coop_battle_find_free_slot`, `$g_coop_battle_slots_online` |
| 5 | Encounter serialization to the slot dict (scene/advantage/garrison/stacks; enemy parties marked `slot_party_coop_battle_slot = slot+1` at `:10555`) | `module_coop_scripts.py` | 10563 | `coop_write_battle_data` |
| 6 | Per-slot dict name (`coop_battle_<slot>.wsedict`) | `module_coop_scripts.py` | 10303 | `coop_battle_slot_dict_name` |
| 7 | battle_available broadcast (ch125 ev 10, 4-int: port `7241+2*slot`, is_initiator, enemy party) | `module_coop_scripts.py` | 9275–9285 | inside launch tail |
| 8 | Client recv ev 10: chooser row + initiator connect flag | `module_coop_scripts.py` | 9200–9230 | `coop_ev_srv_battle_available` → `coop_battle_chooser_set` (`:9224`), `$g_coop_battle_connect_pending` (`:9215`) |
| 9 | B-key chooser menu open | `module_simple_triggers.py` | 74–78 | `$g_coop_battle_available` → `mnu_coop_join_battle` (`module_game_menus.py`) |
| 10 | Deferred auto-connect trigger (game-loop context, not recv handler) | `module_simple_triggers.py` | 83–90 | `$g_coop_battle_connect_pending` → `multiplayer_connect_to_server` |
| 11 | Battle server slot identity: `COOP_BATTLE_SLOT` env → `$coop_battle_slot` (raw-written each poll) | `src/coop_campaign.c` | 361–365 | `g_battle_slot` |
| 12 | Battle server IPC: persistent connect + HELLO{slot} on connect | `src/coop_campaign.c` | 521 | `PKT_BATTLE_HELLO` (`src/shared/battle_ipc.h`) |
| 13 | Battle server: join handler (stamps the hydration window; char load deferred) | `module_coop_mission_templates.py` | 500 | `ti_server_player_joined` |
| 14 | — dict load on first join | `module_coop_mission_templates.py` | 510 | `script_coop_on_admin_panel_load` |
| 15 | — char load via hydration: ch126 identify (ev 54, lo16/hi16 acctid) or 5 s username fallback (spawn-gated, startMission-wipe recovery) | `module_coop_scripts.py` / `module_coop_mission_templates.py` | 1804–1823, 8866 / 65–86, 104–126 | ch126 identify arm → `coop_battle_player_hydrate`; `coop_battle_identify_send`, `coop_battle_hydrate_timeout` |
| 16 | One-life spawn gate + hydration spawn gate (`slot_player_join_time == 0`) | `module_coop_mission_templates.py` | 569–577, 662 | `slot_player_spawned_this_round`, `slot_player_join_time` |
| 17 | Bot reinforcement spawn | `module_coop_mission_templates.py` | ~662–773 | `script_coop_find_bot_troop_for_spawn` |
| 18 | Agent spawn: formation + campaign equip | `module_coop_mission_templates.py` | 877 | `script_coop_spawn_formation`, `script_coop_equip_player_agent` (`module_coop_scripts.py:3243` -- see row 5a below: horse slot fix, 2026-09-07) |
| 19 | Player damage quartered | `module_coop_mission_templates.py` | 160, 413 | `coop_server_reduce_damage` |
| 20 | Death: respawn-as-bot (optional) | `module_coop_mission_templates.py` | ~88–149 | `coop_respawn_as_bot` |
| 21 | Per-death casualty recording | `module_coop_mission_templates.py` | 905 | `script_coop_server_on_agent_killed_or_wounded_common` |
| 22 | Round-end check (A9: reserves, 10s floor, 5s settle; both templates) | `module_coop_mission_templates.py` | 938 | `script_coop_battle_check_round_end` |
| 23 | Dedicated end: results write + kick all | `module_coop_mission_templates.py` | 943–969 | `script_coop_copy_parties_to_file_mp` (def `module_coop_scripts.py:4762`), then `$coop_battle_started = -1` |
| 24 | — per-player name/strength + casualty keys written | `module_coop_scripts.py` | 4874, 4885 | `@battle_player_{i}_name/strength`, `coop_battle_dict_put_stack_cas` |
| 25 | DLL: poll thread sees `$coop_battle_started == -1` | `src/coop_campaign.c` | 527–563 | `battle_poll_thread_func` |
| 26 | DLL: collect + IPC END (slot-tagged) | `src/coop_campaign.c` | 461–496 | `battle_collect_and_save`, `battle_end_signal_t` (`src/shared/battle_ipc.h`) |
| 27 | Campaign DLL: HELLO → peer/slot map + online mask; END → per-slot ended flag | `src/coop_campaign.c` | 666–688, 634–639 | `coop_on_ipc_packet` sets `$g_coop_battle_ended_<slot>`; `update_slots_online_mask`; peer loss for an in-progress slot also sets the ended flag |
| 28 | Rejoin: apply pass loops ALL slots with in_progress or ended set — runs at HYDRATION (ch49 identify ev 8 or 5 s countdown fallback), not at join | `module_coop_scripts.py` | 8919–8926 | `coop_player_hydrate` (def `:8891`); identify arm `:10546–10548`; fallback `module_simple_triggers.py:4526–4546` |
| 29 | Join-time ev-10 re-announce of still-open slots (chooser rebuild; runs at join, BEFORE the hydrate-time apply loop — an `ended==0` filter keeps consumed/finished slots from being re-advertised; is_initiator=0) | `module_coop_scripts.py` | 9095–9125 | dict state in setup_sp..started + roster party present |
| 30 | Results core (once per battle, gated on `end_mp`; participant-gated ev 9 via `@battle_player_{i}_name`) | `module_coop_scripts.py` | 11025, 11050 | `coop_apply_battle_results` |
| 31 | — A7 victory consequences (gold/renown/prisoners/political) + Phase 1 XP pool (victory-gated, `:11135–11137`) | `module_coop_scripts.py` | 10840 | `coop_victory_consequences_dedicated`, `coop_compute_sp_xp_pool_from_dict` |
| 32 | — ally casualties via casualty core; beaten-party release + marker clear | `module_coop_scripts.py` | 7314, 11339–11366 | `coop_apply_player_stack_casualty` (single owner of loss rules) |
| 33 | — abort arm: ended without `end_mp` (battle server lost) → release parties, `abandoned`, slot freed, ev 45 broadcast + ev 44 param 1 to the joiner | `module_coop_scripts.py` | 11403–11447 | `coop_battle_broadcast_slot_closed` (def `:10453`) |
| 34 | Phase 2 per-player pending XP at hydration | `module_coop_scripts.py` | 8935+ | `coop_apply_xp_shares` (inside `coop_player_hydrate`) |
| 35 | Campaign join trigger | `module_simple_triggers.py` | 4448 | `ti_server_player_joined` → `multiplayer_campaign_player_joined` |
| 36 | Startup slot + marker sweep (post-load: all dicts → `none`, flags cleared, stale `slot_party_coop_battle_slot` markers swept — mid-battle autosave restart must not leave parties un-encounterable) | `module_simple_triggers.py` | 40–60 | startup block |
| 37 | Third-party encounter bounce off marked parties (both encounter params tested) | `module_scripts.py` | 2979–2983 | `game_event_party_encounter` |

## State & events

- **Dict files:** `coop_battle_<slot>.wsedict`, one per pool slot 0–3
  (`coop_battle_slot_dict_name`; the battle server derives its own name
  from `$coop_battle_slot`) — keys: `@battle_state` (see states
  below), `@battle_host_party`, `@battle_host_player_name`, `@map_type`
  (battle type), `@map_scn/castle/street/party_id`, `@map_time/cloud/haze/rain`,
  `@battle_adv`, `@tm0_fac/@tm1_fac/@tm1_name`, `@p_garrison`,
  `@p_castle_lord`, `@p_garrison_banner`, `@srvr_set0..11`,
  `@num_parties_enemy/ally`, `@p_enemy{i}_partyid/numstacks/{j}_trp/{j}_num`
  (same for ally), `@num_bots_team_1/2`, `@cls{i}_name`; post-battle:
  `@p_enemy{i}_numstacks_cas` + per-stack cas keys (via
  `coop_battle_dict_put_stack_cas`), `@battle_result`, `@battle_xp_rand`,
  `@battle_num_players`, `@battle_player_{i}_name/strength/acctid`.
- **Char dicts:** `coop_char_sid_<acctid>.wsedict` (Steam-identified) /
  `coop_char_<name>.wsedict` (acctid 0) — naming owned by the
  `coop_char_store_dict_name[_raw]` key-builders. Phase 1 stashes
  `@char_battle_pending`, `@char_pending_party_xp`, `@char_pending_hero_xp`
  (preserved across concurrent saves by `coop_save_character:7073–7099`).
- **Globals (per-slot campaign state):** `$g_coop_battle_in_progress_0..3`
  (module-owned), `$g_coop_battle_ended_0..3` (DLL sets on `PKT_BATTLE_END`
  or IPC peer loss for an in-progress slot; module consumes + clears),
  `$g_coop_battle_slots_online` (DLL-owned bitmask of connected battle-server
  IPC peers — slot allocation reads it, so the pool auto-adapts to how many
  instances are up). Client: `$g_coop_battle_available` (any chooser row),
  `$g_coop_battle_requested`/`$g_coop_battle_connect_pending` (initiator
  auto-connect), chooser table `$coop_avail_*`, `s58`/`s59`. Battle server:
  `$coop_battle_slot` (raw-written from `COOP_BATTLE_SLOT` env each poll),
  `$coop_battle_started` (0 idle, 1 running, -1 dict-flushed),
  `$g_round_ended`, `$coop_winner_team`, `$coop_alive_team1/2`,
  `$coop_battle_state`. The scalar `$g_coop_battle_in_progress`/
  `$g_coop_battle_ended` from the single-server design are gone.
- **Party marker:** enemy parties carry `slot_party_coop_battle_slot` (=600,
  value slot+1) from dict-write until result apply or abort — the third-party
  encounter bounce keys off it.
- **Battle types (dict `@map_type`):** `module_constants.py:1994–1999`
  (field=1, siege attack/defend=2/3, village=4/5, bandit lair=6).
- **Battle states:** `module_constants.py:2002–2008` — none=0, setup_sp=1,
  setup_mp=2, started=3, **end_mp=4** (gates result apply), end_sp=5,
  abandoned=6.
- **Network events:** ch49: `start_battle`=1, `request_siege`=26,
  `leave_encounter`=0 (`header_common.py`). ch125: `encounter_resolved`=9,
  `battle_available`=10 (port, is_initiator, enemy party),
  `battle_rejected`=44 (param 0 = pool full, param 1 = battle server lost
  mid-battle), `battle_slot_closed`=45 (clears a client chooser row);
  id 14, freed by the dead-`return_to_campaign` removal (audit row 7), was
  later reused by A7 as `char_sync_renown` — that per-value event was
  itself deleted by the net-optimizations packed char-sync (renown now
  travels in `packed_misc`, ev 40 — see `xp-sync.md`), so id 14 is free
  again. ch126 battle events:
  `coop_event_*` (`module_constants.py`), incl. A9's ev 52
  `coop_event_battle_retreat`; ch127 ev 53 pushes initiator identity
  per-join. ENet IPC (7242) — the only C-layer wire since B8 (`a68b8ae`):
  `PKT_BATTLE_HELLO` (0x26, `{uint8 slot}`, sent on every (re)connect of a
  battle server's persistent IPC link so the campaign DLL can map
  peer→slot) + `PKT_BATTLE_END` (0x27) with `battle_end_signal_t` (leading
  `uint8 slot` identifies which slot finished; `src/battle_ipc.h`); the
  type byte alone determines wire shape; battle server connects to
  `127.0.0.1:7242` only (`src/shared/battle_net.c`).
- **Ports:** slot s: LAN `7241 + 2*s` (`coop_battle_port_base`,
  `module_constants.py:2015–2016`), Steam `7261 + 2*s` (moved off 7242 —
  it collided with the IPC listener).

## Invariants

- `coop_apply_player_stack_casualty` (`module_coop_scripts.py:7314`) is the
  single owner of player-party battle-loss rules: MP-profile remap, clamp to
  what the party has, surgery saves, wound survivors, heroes never removed,
  `p_player_casualties` accounting. Both callers (dedicated ally loop,
  local-fight arm) must go through it.
- Result application runs **once per battle**: gated on
  `@battle_state == end_mp` and the state is cleared before applying. The
  apply pass loops **all** slots with in_progress or ended set
  (`:8919–8926`) — never just one battle — and runs at HYDRATION
  (`coop_player_hydrate`: ch49 identify or the 5 s countdown fallback),
  post-load, so casualties and spoils only ever touch a hydrated character.
- ev 9 `encounter_resolved` is **participant-gated**: pushed only if the
  joining player's name appears in that battle's `@battle_player_{i}_name`
  list — non-participants never receive a resolve for someone else's battle.
- The join-time ev-10 re-announce (`:9095–9125`) runs at join, BEFORE the
  hydrate-time apply loop; its `ended==0` filter keeps consumed/finished
  slots from being re-advertised. The chooser table is client-side and dies
  with every connection, so only this push can rebuild it on rejoin.
- Enemy parties resolve via the dict `@p_enemy{i}_partyid`, never via
  `party_get_battle_opponent` — the rejoiner's party is REBUILT and has no
  engine battle association. The `slot_party_coop_battle_slot` marker is set
  at dict-write and must be cleared on every exit path (apply, abort, and
  the post-load startup sweep).
- Slot allocation reads `$g_coop_battle_slots_online` — a slot is usable
  only if its battle server's IPC peer is connected AND it is not in
  progress; a pool miss must answer ch125 ev 44 param 0.
- **Documented behavior:** result application is triggered by
  rejoin-then-hydrate, so a
  finished battle whose participants never rejoin holds its slot
  (in_progress stays set) until the campaign server restarts — the startup
  sweep (anchor 36) then resets every slot and clears stale party markers.
- Phase 1 (pool computation + stash) runs once per battle; Phase 2 (apply
  pending XP from char dict) runs on every hydration and clears the pending
  flags before `coop_save_character` re-reads them from disk (`:8935+`).
  Phase 1 stashes for disconnected participants key their char dict via
  `@battle_player_{i}_acctid` (captured at battle-write time) through
  `coop_char_store_dict_name_raw` — participant *matching* stays name-based.
- `coop_save_character` must preserve `@char_pending_*` keys it did not
  create — Phase 1 may stash for players who haven't rejoined.
- `$coop_battle_started = -1` must be assigned only **after** `dict_save`
  of the results (`module_coop_mission_templates.py:943–969`); the DLL treats
  the -1 edge as "dict is on disk".
- `pkt_write`/`pkt_read` frame ALL packets (`src/battle_ipc.h` since B8,
  `a68b8ae`): `[0]=type`, payload at `+PKT_HDR_SIZE`.
- The DLL never calls engine functions from the poll thread — raw reads with
  SEH guard (`src/coop_campaign.c`, battle poll thread).

## Audit: ours vs. native

| # | Behavior | Ours (anchor) | Native ground truth (evidence) | Verdict |
|---|----------|---------------|--------------------------------|---------|
| 1 | Player-party casualty math: surgery re-roll saves a dead unit as wounded at **`P = 0.25 + 0.04 × surgery`** (`:wound_pct = 25 + skl×4`, capped 100); wounded survive; player heroes never removed; enemy heroes wounded 40–70 HP loss. Fixed in `50f4ac1`, runtime-verified 2026-07-10 | `module_coop_scripts.py:7072–7077` (@ `fc1f204`) | RE'd (`patches/Warband/findings.md` "Native kill-vs-wound (surgery) rules", resolver `0x4BB310`): native real-battle/encounter path saves each removed unit as wounded at **`P = 0.25 + 0.04 × surgery`** (constants `0.25`@0x7C1610, `0.04`@0x7C5408), and forces heroes to wounded (`tf_hero` at 0x4BC8EB). Coop now matches base, slope, and the hero rule. Separately, native's bulk `inflict_casualties_to_party_group` opcode (0x54FF80) uses a flat 35% wound / no surgery — coop does not route through it, so that rule is not the relevant baseline. | OK |
| 2 | Post-battle XP pool `(level+10)^2/10` per casualty, cap 40000, × `@battle_xp_rand`/100; split by per-player strength; host share -> `party_add_xp`, joiner share -> hero only | `module_coop_scripts.py:9488–9516`, `:9376–9422`, `:9524–9561` | Native `party_give_xp_and_gold` (`module_scripts.py:15341–15390`) uses the same basis (`p_total_enemy_casualties`, victory call site `module_game_menus.py:4674`) and same per-stack formula + 40000 cap + rand 50–99 roll. Deltas: native scales by `$g_strength_contribution_of_player` **before** the cap, coop caps the raw pool before the strength split; coop's shared roll is 50–100 (`store_random_in_range 50,101` at `:4734`) vs native 50–99. Both are minor and the multi-player split is the intended design. **Native also pays gold (loot share × rand, split among heroes) — coop pays none; tracked under row 6.** | OK |
| 3 | Battle end via shared `coop_battle_check_round_end` (A9, merged `632466c`, runtime-verified 2026-07-25): effective strength = alive agents + temp-party reserves, 10s mission-time floor, 5s settle window since the last agent event (`$coop_last_agent_event_time`), called from 1s triggers in BOTH field-battle and siege templates (old inline instant-wipe checks deleted). Initiator retreat: Esc-menu button (gated on the battle-server-pushed `$g_coop_battle_is_initiator`, ch127 ev 53), request ch126 ev 52 server-verified against `@battle_host_player_name`, ends the round with result 0 (no XP pool). XP pool is victory-gated (`@battle_result == 1`). | `module_coop_scripts.py` `coop_battle_check_round_end`; `module_coop_mission_templates.py` 1s triggers | Native `common_battle_check_victory_condition` (`module_mission_templates.py:889–905`): mission time ≥ 10s + `all_enemies_defeated(5)` + `neg|main_hero_fallen`. RE'd (`patches/Warband/findings.md`, helper `0x00547FE0`): the opcode's parameter is a settle delay since the last agent status change, and it returns FALSE with no local player agent, so it cannot run server-side — the module-side reserve-aware recount mirrors its semantics. Retreat mirrors native's Tab-retreat (casualties applied, no rewards). Delivery caveat fixed en route: ev 126 client->server was dropped by the module's own `game_receive_network_message` client-events gate on dedicated servers (`ea60c7e`); engine RE confirmed no engine-side bound (7-bit wire id, `receiveEvent` 0x004971D0). | OK (A9 merged `632466c`, runtime smoke 2026-07-25) |
| 4 | Spawn: fixed entry-point table per battle type + optional line formation; campaign equip via `coop_equip_player_agent` | `module_coop_mission_templates.py:336–409`, `:726–757`, `:829–893` | Native `lead_charge` uses `mtef_attackers`/`mtef_defenders` team entry flags and engine-standard entry points (`module_mission_templates.py:2218–2244`). Coop's explicit per-battle-type entries + `coop_spawn_formation` are an intentional, admin-panel-configurable MP feature (ch126 `coop_event_spawn_formation`). | OK |
| 4a | `coop_equip_player_agent` (`module_coop_scripts.py:3243`) re-equips the spawned battle agent from the player's saved troop inventory slot-by-slot (needed because a coop player's per-instance dict-loaded gear differs from their static troop template, which is all a plain `agent_new` would otherwise apply). **Fixed 2026-09-07:** both its clear-old-gear and install-armor loops iterated slots `0..7` only -- `ek_horse` is slot 8 (`header_items.py:119`), so a mounted player always spawned dismounted in every battle mission (field, siege, everything) regardless of what was equipped on the campaign map. Bumped both loop upper bounds from 8 to 9; the horse installs through the exact same generic `agent_equip_item` call already used for armor (slot auto-inferred from item kind, no new branch needed). | Native `agent_new`-from-troop-template auto-mounts from the troop's own inventory -- nothing to diverge from once the loop actually covers the horse slot. | OK (fixed 2026-09-07) |
| 5 | One life via spawn gate; death -> optional bot control; all players kicked at battle end | `module_coop_mission_templates.py:557–576`, `:88–149`, `:972–976` | Parked — see Open questions. | PARKED |
| 6 | Encounter resolution: `@battle_result` -> `$g_battle_result` + routed counts, `encounter_resolved` push; the A7 consequence applier pays hero gold shares, renown (enemy strength staged in the dict at write time, value computed at apply — `593fe52`), capacity-capped prisoners, and the political/quest hook on rejoin, dedicated + local paths. The battle-disengage half was FIXED + runtime-verified 2026-07-11 (`f85c30e` + `d865990`): the apply tail releases every dict enemy party from the stale battle association, and on victory clears + `remove_party`s non-center parties, then disengages the (rebuilt) player party. Key mechanism: the rejoining player gets a REBUILT party, so `party_get_battle_opponent` finds nothing — enemy parties must be resolved via the dict `@p_enemy{i}_partyid` | `module_coop_scripts.py` `coop_apply_battle_results` tail | Native victory chain (`module_game_menus.py:4664–4690`): `party_calculate_loot` + loot screen, `party_give_xp_and_gold` (XP **and** hero gold shares), `battle_political_consequences`, `event_player_defeated_enemy_party`, `clear_party_group`. Coop now mirrors it minus item loot and defeat-path consequences (deliberately deferred — `docs/DEFERRED.md`). | OK (A7 merged `4ba5786`, runtime smoke 2026-07-24) |
| 7 | ch125 event 14 `return_to_campaign` constant deleted (`1dc8fec`) — it was never sent or handled; project-state row corrected (ID 14 marked free). Smoke passed 2026-07-11 | `header_common.py` (ID 14 free note) | Confirmed by exhaustive grep of module + C source before removal | OK |
| 8 | Dead `s57` reconnect write deleted from the ASI (`fd0e088`) — nothing consumed it and vanilla scripts clobber `s57` as a scratch register. The live `s59` battle-server-IP write remains. Smoke passed 2026-07-11 | `src/asi/coop.c` `s59_writer_thread` | Confirmed by grep: no consumer in module or src before removal | OK |
| 9 | Legacy `coop_battle.c` orchestration excised (`fd0e088`): file deleted along with the WSELoaderServer spawn, `battle_result.txt` file IPC, Attack-click launch call-site patch, `PKT_BATTLE_START`/`RESULT`, and the party-slot 108–110 result delivery. One orchestration path remains: the ENet-IPC dedicated-server pipeline. Kept live at the time: campaign-ENet INVITE/ACCEPT/DECLINE/RESUME handshake + tick pause and the initiate/cleanup_battle hooks — since deleted in B8 (`a68b8ae`, verified inert); the surviving C layer is the COOP_BATTLE poll thread + `PKT_BATTLE_END` (IPC, 0x27 since `0153ecf`). Smoke 2026-07-11: full chain verified in logs (`battle_started -> -1` → `PKT_BATTLE_END` → `$g_coop_battle_ended = 1` → results applied) | `src/coop_campaign.c`; `src/battle_ipc.h` | The candidate-10 `coop_net`/`battle_net` twin question was closed by deletion in B8 (`a68b8ae`): `coop_net.c` is gone, `battle_net.c` is the sole ENet wrapper | OK |

## Fix list

| # | From audit row | What diverges | Suggested owner/layer |
|---|----------------|---------------|------------------------|
| 0 | 1 | ~~Surgery save omits native's 0.25 base~~ Fixed (`50f4ac1`) + runtime-verified 2026-07-10: `coop_apply_player_stack_casualty` rolls `25 + skl×4` percent, capped 100. | `module_coop_scripts.py:7072–7077` |
| 1 | 7 | ~~Dead ev 14 `return_to_campaign`~~ **Done** (`1dc8fec`, smoke 2026-07-11): constant removed, project-state row corrected. | `header_common.py` + workbench project-state doc |
| 2 | 8 | ~~Dead `s57` reconnect write~~ **Done** (`fd0e088`, smoke 2026-07-11): write deleted; auto-rejoin, if ever wanted, gets its own design. | `src/asi/coop.c` |
| 3 | 3 | ~~Round-end reserve-blind and instant; no retreat path~~ **Done** (A9, merged `632466c`, runtime smoke 2026-07-25): shared `coop_battle_check_round_end` (reserves + 10s floor + 5s settle) in both templates; victory-gated XP pool; initiator retreat (ch126 ev 52 / ch127 ev 53). Deferred: legacy `coop_event_end_battle` admin check, no retreat fallback on initiator disconnect (`docs/DEFERRED.md`). | `module_coop_scripts.py` + `module_coop_mission_templates.py` |
| 4 | 6 | ~~Victory pays XP only~~ **Done** (A7, merged `4ba5786`, runtime smoke 2026-07-24): hero gold shares, renown, capacity-capped prisoners, and the political/quest hook now applied on rejoin (dedicated + local). Item loot and defeat-path consequences deliberately deferred (`docs/DEFERRED.md`). ~~Urgent sub-piece: server-side battle disengage + beaten-party removal~~ **Done** (`f85c30e` + `d865990`, runtime-verified 2026-07-11) — resolved via dict party ids; the ev-17 local arm got the same `remove_party` treatment (`d82d879`). | `module_coop_scripts.py` BATTLE PIPELINE section |
| 5 | 9 | ~~Legacy `coop_battle.c` orchestration~~ **Done** (`fd0e088`, smoke 2026-07-11): excised (see audit row 9). Candidate 10 (`coop_net`/`battle_net` merge) remains deferred. | `src/coop_campaign.c` + `src/battle_ipc.h` |
| 6 | 4a | ~~Player always spawns dismounted in battle missions~~ **Fixed 2026-09-07**: `coop_equip_player_agent`'s two gear loops bumped from `0..7`/`4..7` to `0..8`/`4..8` (upper bound arg 9) to cover `ek_horse` (slot 8), found by exhaustive-grep of the equip script after a user report ("horse doesn't work in coop battles" -> narrowed to "spawn on foot with no horse"). **This did NOT actually fix it** -- a later user report (2026-09-10, regular dedicated battles specifically) with the equip pipeline's own `[EQUIP SNAPSHOT]` log confirmed weapons loaded correctly (`weapons=434,548,151,-1`), ruling out the campaign->battle-server equipment-transfer pipeline (verified end to end: `coop_write_battle_data`'s freeze loop, `coop_battle_load_equipment_snapshot`'s load loop, and this equip loop are ALL correctly ranged `0..9`) -- the horse item genuinely reaches the troop record, it just never mounts the agent. Suspected root cause: `coop_equip_player_agent`'s `agent_equip_item` call is a post-spawn, singleplayer-era op applied here as a retrofit (its own comment already flags it "#ITEM BUG WORKAROUND") -- reliable for weapons/armor, apparently not for mounting. **Attempted fix 2026-09-10, reverted the same day**: pre-armed the horse via native's proper multiplayer mechanism, `player_add_spawn_item(player_no, ek_horse, item)`, right before `player_spawn_new_agent` in both `mt_coop_battle` and its siege mirror. This DID something, but not the right thing: a follow-up user report showed it displaced the player's real weapon with a spear/lance -- this mod has its own pre-existing "auto-forcing troop class" mechanic (per the user, not yet located/understood in this codebase) that appears to react to a staged spawn-item by committing a native cavalry-class default loadout over slot 0, instead of the horse merely being added alongside existing gear. Net worse trade (a working sword lost for a working horse), so reverted. **Root cause actually identified 2026-09-10, via a direct user sighting**: the "auto-forcing troop class" mechanic is native's own real multiplayer troop-class-selection screen (Infantry/Archer/Cavalry etc, `prsnt_multiplayer_troop_select`/`prsnt_multiplayer_item_select`, `module_presentations.py`) -- confirmed by the user actually seeing it appear. `mt_coop_battle`'s own spawn trigger already had a comment acknowledging "the obsolete selection menu" exists and is kept from *blocking* the spawn timer, but it was never actually suppressed from *displaying* or from applying its own class-default item selections (`script_multiplayer_buy_agent_equipment`, `module_scripts.py:13841` -- fills any weapon/mount category the chosen class doesn't already have selected with a native default, e.g. a spear for a non-cavalry class, no horse for a foot class) -- explains both symptoms as one root cause, not two. Exact trigger/timing of when this native flow re-applies its own equip choices relative to `coop_equip_player_agent`'s single pass at `ti_on_agent_spawn` couldn't be pinned down without a debugger. **Fix attempt 1 (2026-09-10, wrong)**: a one-shot (`ti_once`) trigger meant to re-call `coop_equip_player_agent` ~1.5s after spawn. Confirmed live to never fire at all (no diagnostic output whatsoever) -- `ti_once` fires on the *first* check where its condition passes, and the condition used (`multiplayer_is_server`) is already true from the very start of the mission, so it fired almost immediately, before any player had spawned, found no agents in its loop, and (being one-shot) never got a second chance. **Fix attempt 2 (2026-09-10, same day)**: dropped `ti_once` entirely, checking every second for the whole battle instead, comparing each player's live agent weapon-0/horse slots against their troop's real inventory (source of truth) and only re-equipping (and logging) on an actual mismatch. This diagnostic proved decisive: over 20 consecutive per-second retries across 24 real seconds, weapon slot 0 always matched (ruling out the class-selection-screen timing theory for weapons, at least in that run) while the horse slot stayed 0 every single time, proving `agent_equip_item` genuinely cannot mount a horse on an already-spawned agent -- not a timing race, a real limitation of that op for this purpose. **Fix attempt 3 (2026-09-10, same day)**: this vindicates attempt-1's original mechanism (`player_add_spawn_item`, native's proper pre-spawn staging op) for the horse specifically -- attempt 1's regression (spear replacing the real weapon) came from staging *only* the horse through it, leaving every weapon/armor slot unselected for native's own class-default-fill logic (`script_multiplayer_buy_agent_equipment`) to fill in. This attempt stages every slot (`ek_item_0..ek_horse`) the player actually has real gear in via `player_add_spawn_item`, not just the horse, so nothing should be left for that fill-in logic to act on. **Real regression from attempt 2's own "safety net", found live (2026-09-10, same day)**: with the horse mismatch persisting every second (before attempt 3 landed), attempt 2's trigger called `coop_equip_player_agent` every second -- and that function's "unequip everything, re-equip from inventory slot order" approach also resets whichever weapon the player currently has drawn back to their first slot. Reported directly: switching weapons mid-fight silently reverted to the first weapon within a second, every time. **Fixed same day**: the repeating trigger is now diagnostic-only -- it still logs a mismatch every second if one exists, but no longer calls `coop_equip_player_agent` at all, so it can never again interfere with weapon switching. Safe to delete entirely once logs confirm attempt 3's pre-spawn staging keeps the horse correct with no mismatch ever logged.

**Attempt 3 partially worked (horse confirmed mounted in a later test) but the sword/spear substitution persisted, plus a new symptom: an unowned shield also appeared.** `multiplayer_buy_agent_equipment` (`module_scripts.py:13841`) doesn't only fill in *unselected* categories -- for a category that *is* selected, it separately checks affordability against `player_get_gold`, a native multiplayer currency completely distinct from the campaign gold this coop mod tracks via `troop_gold` (and never itself sets). If that native gold defaults to 0 for every connecting player, EVERY real staged item would be judged unaffordable, and native's own documented behavior for that case is to substitute the cheapest default item in the same class -- a sword downgraded to a spear is exactly that rule, not a random injection; an unowned shield appearing fits a similar substitution for the shield-class slot. **Fix attempt 4 (2026-09-10, same day)**: `player_set_gold(player_no, multi_max_gold_that_can_be_stored, ...)` added right before the per-slot staging loop, so nothing real can ever be judged unaffordable by that check. Diagnostic troop-id logging also added to both `[EQUIP SNAPSHOT]` (`module_coop_scripts.py`) and `[REEQUIP DEBUG]` (`module_coop_mission_templates.py`) to independently verify whether a still-unexplained slot-0 discrepancy between connect-time and battle-time (434 vs 548 for the same player, seen consistently before this fix) is the same troop object or two different ones. **Attempt 4 disproven by live test (2026-09-10, same day)**: user confirmed testing the gold-cap build; the shield+spear symptom persisted unchanged, ruling out the native-MP-gold-affordability theory entirely (native gold was never the cause of the substitution, regardless of whether it defaults to 0). One real, confirmed-by-screenshot win did come out of attempts 3+4 together, though: **the horse now mounts reliably** (agent HUD screenshot showed a live horse health bar) — attempt 3's per-slot `player_add_spawn_item` staging is the part that actually fixed the horse; the gold cap in attempt 4 was inert. **Second big finding (2026-09-10, same day)**: item id lookup in `ID_items.py` showed `548 = itm_hunting_bow`, not a spear as visually assumed from the HUD icon at small size — the player's real loadout (`sword 434 / bow 548 / arrows 151 / horse 143`) is entirely normal gear, and the troop-id-tagged `[REEQUIP DEBUG]`/`[EQUIP SNAPSHOT]` logs confirmed both diagnostics read the *same* troop object (`troop=7`) at both connect-time and battle-time, ruling out a two-different-troops mixup. This reframes the whole investigation: the "spear" was very likely the bow's own icon read wrong at a glance, and the real open question is **where the confirmed-unowned shield is coming from**, since the `[REEQUIP DEBUG]` diagnostic had only ever compared weapon slot 0 (`wpn0`) — slots 1-3, where a bow/shield would actually sit, were never logged at all. **Diagnostic expanded 2026-09-10, same day**: both the `mt_coop_battle` and siege-mirror repeating triggers now read and log all 4 weapon slots (0-3) plus the horse, for both the live agent and the troop's real inventory, instead of only slot 0/horse — full visibility into whether this is a slot reorder (item present, wrong slot) or a genuine foreign item (present in no troop slot at all). **Also found and fixed in the same pass**: the siege-template copy of this trigger had silently drifted out of sync with `mt_coop_battle`'s — it still used the broken one-shot `ti_once` timing (Fix attempt 1, proven dead) AND still called the corrective `coop_equip_player_agent` every fire (the exact op proven to cause the weapon-switch-reset regression), meaning siege battles specifically never got either the timing fix or the regression fix that field battles received. Brought in line with the field-battle copy: repeating `(1,0,0,...)`, diagnostic-only, no corrective call, all 4 slots. Awaiting a fresh playtest log with the expanded output before further theorizing. | `module_coop_mission_templates.py` (both player-spawn triggers; repeating 1s diagnostic trigger in both `mt_coop_battle` and its siege mirror now expanded to all 4 weapon slots + horse, diagnostic-only, siege copy's stale `ti_once`/corrective-call drift fixed); `module_coop_scripts.py:3243`, `module_coop_scripts.py` `coop_battle_load_equipment_snapshot` (troop-id added to its own log line) |
| 7 | 4b | ~~"Select Troop" screen sometimes doesn't auto-close~~ **Fixed 2026-09-08**: this mod force-assigns each connected player their own hydrated campaign troop server-side (`coop_battle_player_hydrate`, `module_coop_scripts.py:10131-10186` -- native's own troop-selection is not authoritative for coop spawning) and closes the native `prsnt_coop_troop_select`/`prsnt_coop_team_select`/`prsnt_coop_commander_select` presentation client-side once that assignment arrives. The close was gated on a one-shot flag (`$coop_force_close_troop_select`) set exactly once by the network receive handler (`module_coop_scripts.py:2682-2714`). But `module_coop_presentations.py`'s team-select confirmation flow (~1637-1646) can independently reopen `prsnt_coop_troop_select` later -- it only skips reopening if `slot_player_coop_selected_troop` already reads as set locally, which can still be unset if that confirmation fires before the server's force-assign broadcast lands (hydration can take up to ~5s for a non-identifying client, `module_coop_mission_templates.py:107-151`). Once that race-reopened instance appears, the already-spent one-shot flag never re-arms, so nothing closes it. Fixed by having the 0.1s backstop trigger (`module_simple_triggers.py`) check the player's actual forced-troop state directly every tick instead of only reacting to the single edge-triggered flag -- closes any reopened instance regardless of event ordering. Not yet playtest-confirmed. | `module_simple_triggers.py` (0.1s trigger) |

## Open questions

- Audit row 5 (engine per-player mission state after the end-of-battle mass
  kick): parked — impact is bounded because the battle server immediately
  restarts its mission and clients fully reconnect to the campaign server;
  the wave-2 runtime smoke test exercises exactly this path and is the
  cheaper verification.

## Related docs

Workbench documents (not part of the public export — see the citation
note in `README.md`):

- `docs/archive/BATTLE_RESULTS_PIPELINE_AUDIT.md` — earlier results-pipeline audit.
- `docs/archive/2026-03-22-warband-coop-campaign-sync-design.md`
  — original campaign-sync design.
- `patches/WarbandDedicated/kb.h` / `findings.md` — campaign-server binary RE.
