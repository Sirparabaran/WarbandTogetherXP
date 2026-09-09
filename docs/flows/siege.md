# Flow: Siege (coop siege battle types + local siege)

**Status:** AUDITED
**Validated against commit:** `ce0e287` (steamid persistence,
runtime-verified 2026-08-23: the `@char_siege_center` stash lives in the
now sid-or-username-keyed char dict — persistence reference only. Prior
stamp `0b2500a`)

## Scope

How a coop player assaults a town/castle: the **dedicated siege** (battle
server runs the `coop_siege` mission) and the **local siege** (initiating
client fights an SP-style wall assault against AI, then reports the result).
Entry points: the coop center menu options `coop_center_assault` /
`coop_center_assault_local`. Exit state: garrison casualties applied (both
paths); on a win, both paths run the A7 victory applier plus the shared
capture-consequences script (`coop_siege_capture_consequences`).
The shared serialization/IPC/result tail is documented in
`battle-pipeline.md` — this dossier covers only the siege delta.

Module paths relative to `wse2work/Native-Coop-master/`; C paths relative to
repo root.

## Sequence diagram

```mermaid
sequenceDiagram
    participant C as Client (assaulting player)
    participant CS as Campaign Server
    participant BS as Battle Server

    rect rgb(235,235,245)
    note over C,BS: Dedicated siege
    C->>CS: ch49 request_siege (26) with center party
    C->>BS: immediately connects to {s59}:7241
    CS->>CS: save all chars, coop_write_battle_data(type=2):<br/>scene from center slots, garrison = center party as enemy0,<br/>@p_castle_lord/@p_garrison keys
    CS->>C: ch125 battle_available (10) to other players
    BS->>BS: coop_siege mission: belfry/ladder setup,<br/>rounds (walls → street → hall) on listen,<br/>dedicated forced single-round
    BS->>CS: dict + $coop_battle_started=-1 → IPC END<br/>(shared tail, see battle-pipeline.md)
    CS->>CS: apply_battle_results: garrison casualties,<br/>A7 applier (win), then coop_siege_capture_consequences:<br/>capture + prosperity/war damage/renown +5
    end

    rect rgb(235,245,235)
    note over C,CS: Local siege
    C->>CS: ch49 request_siege_local (27) with center party
    CS->>CS: validate town/castle, wall scene from center slots,<br/>stash @char_siege_center in char dict, save char, freeze party
    CS->>C: ch125 start_siege_local (42): wall_scene, with_belfry
    C->>C: coop_client_start_siege_local:<br/>mt_coop_castle_attack_walls_{belfry|ladder},<br/>debrief menu, $g_coop_asi_local_battle=1
    C->>CS: ch49 local_fight_result (17): win/loss, casualties, xp delta
    CS->>CS: win: A7 applier (gold/renown/prisoners/political),<br/>then coop_siege_capture_consequences
    end
```

## Code anchors

| # | Step | File | Line | Symbol |
|---|------|------|------|--------|
| 1 | Dedicated assault menu option | `module_game_menus.py` | 14790–14807 | `coop_center_assault` (sends ch49 ev 26, connects to `{s59}:7241`) |
| 2 | Local assault menu option | `module_game_menus.py` | 14809–14821 | `coop_center_assault_local` (sends ch49 ev 27) |
| 3 | Server arm: request_siege | `module_coop_scripts.py` | 8600–8629 | save chars, `coop_write_battle_data(type=2)`, battle_available |
| 4 | Server arm: request_siege_local | `module_coop_scripts.py` | 8741–8779 | wall scene from `slot_town_walls`/`slot_castle_exterior`, `@char_siege_center` stash via `coop_char_siege_center_set` (`:7558`), freeze party, ch125 ev 42 |
| 5 | Client local-siege launcher | `module_coop_scripts.py` | 6955–6993 | `coop_client_start_siege_local` -> `mt_coop_castle_attack_walls_{belfry\|ladder}` |
| 6 | Local siege client missions | `module_mission_templates.py` | 3232, 3309 | `coop_castle_attack_walls_ladder` / `_belfry` |
| 7 | Battle dict siege branch: scenes | `module_coop_scripts.py` | 9081–9098 | town: walls+castle+street slots; castle: exterior |
| 8 | Battle dict siege branch: garrison keys | `module_coop_scripts.py` | 9177–9193 | `@p_castle_lord`, `@p_garrison` (=0: enemy-party index), `@p_garrison_banner` from town lord |
| 9 | Dict loader battle-type branches | `module_coop_scripts.py` | 85, 136, 155 | `coop_on_admin_panel_load` (`$coop_battle_type`, `$coop_map_party`) |
| 10 | Battle server siege template | `module_coop_mission_templates.py` | 4500 | `coop_siege` |
| 11 | — siege join/bootstrap, belfry wheel init | `module_coop_mission_templates.py` | 4596–4649 | `$coop_use_belfry`, `multiplayer_initialize_belfry_wheel_rotations` |
| 12 | — round progression (listen mode) | `module_coop_mission_templates.py` | 5150–5232 | `$coop_round`, `coop_player_agent_save_items` (:5199), next scene by round (:5222–5228) |
| 13 | — dedicated forced single-round + end | `module_coop_mission_templates.py` | 5153–5157, 5265–5291 | |
| 14 | — belfry placement block | `module_coop_mission_templates.py` | 5391–5423 | `script_coop_move_belfries_to_their_first_entry_point` (def `module_coop_scripts.py:3187`) |
| 15 | — belfry crew assignment | `module_coop_mission_templates.py` | 5469 | `script_cf_coop_siege_assign_men_to_belfry` |
| 16 | Round-type / reserve constants | `module_constants.py` | 2101–2112 | `coop_reserves_hall/street`, `coop_round_*` |
| 17 | Local siege aftermath (event 17 arm) | `module_coop_scripts.py` | 9866–9889 | `coop_char_siege_center_get` + clear; win: `coop_victory_consequences_local` then `coop_siege_capture_consequences` + renown +5 |
| 18 | Dedicated siege result apply (shared) | `module_coop_scripts.py` | 11292–11325 | `coop_apply_battle_results` — garrison casualties via `@p_enemy0_partyid` = center party; win: A7 applier first, then `coop_siege_capture_consequences` (captor = initiator via `@battle_host_player_name`, applier fallback) |
| 19 | Shared capture consequences | `module_coop_scripts.py` | 9786–9823 | `coop_siege_capture_consequences`: `party_clear`, `slot_center_last_taken_by_troop`, prosperity −5, war damage 40/20 vs old faction, full `give_center_to_faction` to `fac_player_faction` |

## State & events

- **Dict keys (siege delta):** `@map_type` ∈ {2 attack, 3 defend},
  `@map_scn` (walls), `@map_castle`, `@map_street`, `@map_party_id`
  (besieged center), `@p_castle_lord`/`@p_garrison` (enemy-party indices,
  0 for sieges, -1 for field), `@p_garrison_banner`.
- **Dict key:** `@char_siege_center` in the player's char dict — the
  locally-assaulted center; survives the client's disconnect AND the
  player_no/party reassignment on rejoin (preserved across
  `coop_save_character` rebuilds; helpers `coop_char_siege_center_set/_get`
  `module_coop_scripts.py:7558/:7573`; cleared by ev 16 and ev 17).
- **Slots:** `slot_center_coop_lock_player` — center encounter lock;
  `slot_center_siege_with_belfry` (`:261`) — picks belfry vs ladder variant;
  `slot_town_walls`/`slot_castle_exterior`/`slot_town_castle`/`slot_town_center`
  — scene sources.
- **Globals:** `$coop_round` (siege phase), `$coop_use_belfry`,
  `$belfry_positioned`, `$g_coop_asi_local_battle` (client),
  `$g_coop_center_party`/`$g_coop_center_type`/`$g_coop_center_scene`
  (client center-menu state).
- **Round types:** `coop_round_battle`=1, `stop_reinforcing_wall`=2,
  `town_street`=3, `stop_reinforcing_street`=4, `castle_hall`=5
  (`module_constants.py:2108–2112`); reserves: hall 20, street 80
  (`:2103–2104`).
- **Network events:** ch49 `request_siege`=26, `request_siege_local`=27,
  `local_fight_result`=17; ch125 `battle_available`=10,
  `start_siege_local`=42 (`header_common.py:216`, `:248–251`). Battle types:
  `module_constants.py:1995–1999`.

## Invariants

- The local-siege target lives in the **per-player char dict**
  (sid- or username-keyed since `ce0e287`; key-builder-owned naming)
  (`@char_siege_center`) because the client disconnects into its local
  mission and rejoins with a NEW player_no and slot-derived party — player
  and party ids identify nothing across that boundary (runtime-proven:
  player 3/party 6 at assault became player 5/party 8 at result). A field
  local fight (ev 16) clears any stale stash so an abandoned siege can't
  be misread as a capture.
- The wall scene must be sent from the server (`:8662–8666`) — center party
  slots are not synced to clients. This same invariant is why Vassalage
  Phase 6's settlement-management menu (`docs/flows/vassalage.md`) can't
  read construction/garrison state locally either — it feeds its menu
  entirely from an explicit server snapshot push instead of a local
  `party_get_slot` read on the center.
- Dedicated siege is single-round by design gate (`:5153–5157`); multi-round
  progression (walls -> street -> hall) exists only in listen mode.
- Local-siege party freeze (`disable_party` `:8656–8661`) must be undone by
  rejoin/`enable_party` (`multiplayer_campaign_player_joined:8164`) or exit
  save (`multiplayer_campaign_player_exit:8250`).
- For sieges the besieged center **is** the serialized enemy party 0;
  garrison casualty application therefore mutates the center party directly
  (`coop_apply_battle_results` via `@p_enemy0_partyid`).

## Audit: ours vs. native

| # | Behavior | Ours (anchor) | Native ground truth (evidence) | Verdict |
|---|----------|---------------|--------------------------------|---------|
| 1 | Siege scene + variant: walls scene from `slot_town_walls`/`slot_castle_exterior`, belfry vs ladder from `slot_center_siege_with_belfry` | `module_coop_scripts.py:9086–9094`, `:8642–8651` | Identical sources to native: assault menus read `slot_town_walls`/`slot_castle_exterior` (`module_game_menus.py:5455–5457`, `:5815–5820`) and the belfry flag is the same slot native seeds at game start (`module_scripts.py:346–360`). Local-siege missions are literal copies of native `castle_attack_walls_{belfry,ladder}` with only a side-flag swap (`module_mission_templates.py:3222–3232`, `:3302–3309`). | OK |
| 2 | Belfry (dedicated `coop_siege`): pre-positioned at first entry point with `slot_scene_prop_belfry_platform_moved=1`; no push/rotate phase | `module_coop_mission_templates.py:5391–5423` (native trigger commented out at `:5391`), `:5469`; `module_coop_scripts.py:3187` | Native uses `common_siege_init_ai_and_belfry`/`_move_belfry`/`_rotate_belfry`/`_assign_men_to_belfry` (`module_mission_templates.py:981–1002`) — soldiers push the belfry to the wall. Coop deliberately replaces this (native triggers commented out, replacement block inline): belfries start at the wall, movement phase skipped. Intentional MP simplification. | OK |
| 3 | Defender composition: center party as enemy0 (garrison keys point there) plus every party attached to the center serialized as enemy1..N with per-party casualty round-trip via `@p_enemy{i}_partyid`; friendly AI parties within `coop_siege_join_radius` join as ally1..N (placeholder rule until a siege camp exists) | `coop_write_battle_data` roster sections + `script_coop_battle_dict_write_roster_party` | Native battles collect **attached parties** into the enemy side (`party_collect_attachments_to_party` -> `p_collective_enemy`, `module_game_menus.py:6045`, `:4247`). Coop now rosters them per-party (identity preserved for casualty apply-back — a collective party can't round-trip through the dict). Fixed `67239e5..d7955b7`, runtime-verified 2026-07-19 multi-client. Attacker-side filter tightened in A8 (`2845589`, verified 2026-07-25): lord-party whitelist + real-ally relation rule | OK |
| 4 | Multi-round siege (walls -> street -> hall) in listen mode; dedicated forced single-round | `module_coop_mission_templates.py:5150–5232`, `:5153–5157` | Dedicated single-round is documented in-code: "Dedicated siege v1 is single-round: the wall battle decides the siege" (`:5153–5155`). The multi-phase progression is inherited coop-mod (Banner Time) design, a deliberate extension over native's single-scene assault. | OK |
| 5 | Dedicated siege victory: capture fixed in `50f4ac1` (A5, runtime-verified 2026-07-10); A8 (`0b2500a`, runtime-verified 2026-07-25) moved it into the shared capture script and reordered it AFTER the A7 applier — political consequences now read the center's pre-transfer faction (latent A7-era ordering bug fixed) | `module_coop_scripts.py:11292–11325` | Native: successful assault transfers the center, handles lord capture/escape, prisoners, and post-siege menus — A7 applier + A8 capture script now cover the set | OK |
| 6 | Both capture paths run native `castle_taken` consequences via `coop_siege_capture_consequences`: `party_clear`, `slot_center_last_taken_by_troop` (initiator), prosperity −5, renown +5 to every participant, war damage 40/20 vs the old owner, full `give_center_to_faction`; local siege additionally gained the previously-missing A7 applier call (gold/battle renown/garrison prisoners/lord fate/political). Fixed A8 (`8b45f2a..db4a815`, merged `0b2500a`), runtime-verified 2026-07-25 (Bulugha Castle smoke: gold+renown+capture; prisoners confirmed once prisoner_management > 0 — native cap) | `module_coop_scripts.py:9786–9823`, `:9866–9889` | Native `castle_taken` menu (`module_game_menus.py:6569–6640`). Deliberately dropped as N/A in coop: `lift_siege` (no siege-camp state), besieger guard order (no besieger AI). **Superseded 2026-09-06 for the keep-or-give-to-vassal case**: `docs/flows/vassalage.md` Phase 2 -- a captor who has sworn fealty (Phase 1) now personally receives the center under their own kingdom instead of always `fac_player_faction`; unsworn captors are unchanged. Known cosmetic: local path prints both "has fallen" and "has captured" | OK |
| 7 | Battle types `siege_player_defend` (3), `village_*` (4/5), `bandit_lair` (6) are defined and handled by the dict loader, but nothing ever launches them | `module_constants.py:1996–1999`; only callers pass types 1/2 (`module_coop_scripts.py:8578`, `:8617`) | Confirmed by exhaustive grep: no `coop_write_battle_data` caller uses types 3–6. Group-C resolution (`1dc8fec`): kept as future-feature scaffolding, documented as "defined, not yet launched" here and in project-state — the divergence was the docs overstating them, now corrected. **`village_player_attack` (4) wired up 2026-09-08** (see Fix list #5) — user reported a hostile village had no action beyond "Leave.", unlike towns/castles' existing "Lay siege" option. `siege_player_defend`(3)/`village_player_defend`(5)/`bandit_lair`(6) remain unlaunched scaffolding | OK |

## Fix list

| # | From audit row | What diverges | Suggested owner/layer |
|---|----------------|---------------|------------------------|
| 1 | 5 | ~~Winning a dedicated siege does not capture the center~~ **Done**: capture in `50f4ac1` (A5); A8 (`0b2500a`) moved it into `coop_siege_capture_consequences`, ordered after the A7 applier. | `module_coop_scripts.py` BATTLE PIPELINE section |
| 2 | 6 | ~~Local-siege capture skips native consequences~~ **Done** (A8, `8b45f2a..db4a815`, merged `0b2500a`, runtime-verified 2026-07-25): shared capture script + A7 applier call on the local path. Residual (DEFERRED.md): local sieges never roster defender attachments; garrison stacks leak through the ev-17 `-2` path. | `module_coop_scripts.py` |
| 3 | 7 | ~~Dead battle types 3–6~~ **Done** (`1dc8fec`): kept as future-feature scaffolding, documented as "defined, not yet launched" (code untouched). Wire launch paths when the features are built. | `module_constants.py` + `module_coop_scripts.py` |
| 4 | 3 | ~~Attached defender parties excluded from the battle roster~~ **Done** (`67239e5..d7955b7`, runtime-verified 2026-07-19): attachments serialized as enemy1..N, proximity AI allies as ally1..N, casualties routed per party, markers on all rostered parties + startup sweep. Attacker-filter refinement **done** (A8 `2845589`, runtime-verified 2026-07-25): `spt_kingdom_hero_party` whitelist + same-faction-or-relation>0 rule; farmer/third-faction parties excluded, allied lords still join. | `module_coop_scripts.py` `coop_write_battle_data` |
| 5 | 7 | ~~Villages had no hostile action, only "Leave."~~ **Fixed 2026-09-08**: new "Raid the village {s1}!" item in `mnu_coop_center_locked` (villages only, siblings the existing town/castle "Lay siege" item, same `request_siege` event, no new network message). `coop_ev_cli_request_siege` now branches on `slot_party_type` to pick `coop_battle_type_village_player_attack` instead of the siege type for a village target. The village's own party was already unconditionally serialized as `@p_enemy0_*` regardless of battle_type; the only real gap was `coop_write_battle_data`'s `@p_castle_lord`/`@p_garrison` index assignment being siege-type-only, so a village battle never got a valid garrison index fed to the already-existing village-type admin-panel-load handling (`:138-150`) -- extended that condition to include the village types too. Reuses the generic victory-consequences path unchanged (XP/gold from casualties, same as any other battle type) -- no native-style loot/prosperity/relation consequences this pass, by explicit choice. Not yet playtest-confirmed. | `module_game_menus.py` `mnu_coop_center_locked`; `module_coop_scripts.py` `coop_ev_cli_request_siege`, `coop_write_battle_data` |
| 6 | -- | ~~"Fight locally (Singleplayer)" permanently disabled on the encounter screen (`(eq, 1, 0)`), comment claiming it disconnects/rebuilds the character~~ **Fixed 2026-09-10**: the local-fight debrief menu (`mnu_coop_local_battle_debrief`, module_game_menus.py) and its whole result-relay pipeline (globals -> `coop_local_result.ini` via the ASI -> `$g_coop_pending_local_result` replay in `module_simple_triggers.py` -> `local_fight_result`) were already built to tolerate a lost connection -- see `battle-pipeline.md`'s reconnect section -- but nothing ever made the reconnect itself automatic; the player had to manually rejoin from the MP browser. Re-enabled the menu item and armed `$g_coop_return_to_campaign` (the same flag a dedicated battle server's own end-of-battle kick arms, driving the ASI's invite auto-join hop) on the debrief's "Continue" button. Same arm added as a safety net to `coop_local_visit_exit` (`module_coop_repairs.py`, shared by every settlement-visit type -- tavern/town/village/castle/lords-hall), whose own comment already flagged a teardown-timing disconnect race that its commit-before-exit ordering only mitigates, not eliminates. | `module_game_menus.py` `encounter_fight_sp`, `mnu_coop_local_battle_debrief`; `module_coop_repairs.py` `coop_local_visit_exit` |
| 7 | -- | ~~Entering the Lord's Hall, then pressing Tab, re-opened the stale "Preparing the Lord's Hall" menu instead of cleanly leaving~~ **Fixed 2026-09-10**: two independent bugs found in the same custom mission template. (a) `coop_visit_hall`'s `ti_tab_pressed` override (`module_mission_templates.py`, appended after `mission_templates`) called bare `finish_mission` -- despite its own comment claiming parity with "the working settlement templates" (tavern/town/village), it never called `script_coop_local_visit_exit` the way those actually do, so `local_visit_done` never reached the server and `$g_coop_in_local_visit` was left stuck at 1. (b) `coop_enter_synced_hall` (`module_coop_repairs.py`) is uniquely launched from an intermediate `mnu_coop_hall_wait` "Preparing..." menu rather than straight from `mnu_coop_center_encounter` like every other local-visit entry point -- so with no destination queued before `change_screen_mission`, `finish_mission` naturally popped back to that stale waiting screen (still showing `$g_coop_hall_pending` from before entry) instead of the settlement menu. | `module_mission_templates.py` `coop_visit_hall` trigger override; `module_coop_repairs.py` `coop_enter_synced_hall` |
| 8 | -- | ~~Post-local-fight loot screen never opened at all~~ **Fixed 2026-09-10** (C-layer, `src/asi/coop.c`) -- see `inventory-sync.md` audit row 16 / fix-list row 15 for the full writeup: the pending-result module globals the local-fight relay pipeline (row 6 above) depends on were being wiped by the very reconnect they raced against. | `src/asi/coop.c` |

## Open questions

None — all audit rows resolved module-side (no engine RE was required for
this flow).

## Related docs

- `battle-pipeline.md` — shared serialization, IPC, and result-apply tail.

Workbench documents (not part of the public export — see the citation
note in `README.md`):

- `docs/plans/siege-coop-plan.md` — earlier siege planning notes.
- `docs/archive/2026-03-23-warband-coop-party-creation-design.md`
  — party-creation design notes.
