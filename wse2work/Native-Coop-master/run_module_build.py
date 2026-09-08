"""Run the full compiler, failing on errors even in legacy processors."""
from __future__ import print_function
import os
import re
import subprocess
import sys

STAGES = [
    'check_network_arity',
    'check_campaign_protocol',
    'process_init', 'process_global_variables', 'process_strings',
    'process_skills', 'process_music', 'process_animations', 'process_meshes',
    'process_sounds', 'process_skins', 'process_map_icons', 'process_factions',
    'process_items', 'process_scenes', 'process_troops', 'process_particle_sys',
    'process_scene_props', 'process_tableau_materials', 'process_presentations',
    'process_party_tmps', 'process_parties', 'process_quests', 'process_info_pages',
    'process_scripts', 'process_mission_tmps', 'process_game_menus',
    'process_simple_triggers', 'process_dialogs', 'process_global_variables_unused',
    'process_postfx',
]


def has_errors(output):
    # Some old processors print ERROR but return exit code zero.
    return bool(re.search(r'(?im)^\s*(?:ERROR\b|Traceback \(most recent call last\))', output))


def main(stages=None):
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    for stage in STAGES if stages is None else stages:
        process = subprocess.Popen([sys.executable, '-B', stage + '.py'],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        raw, unused = process.communicate()
        output = raw.decode('utf-8', 'replace')
        sys.stdout.write(output.encode('ascii', 'replace').decode('ascii'))
        if process.returncode or has_errors(output):
            print('BUILD FAILED in %s. Do not deploy this partial export.' % stage)
            return 1
    print('Full module build succeeded.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
