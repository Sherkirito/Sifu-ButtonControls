"""Build the customizer catalogue from this build's unmodified local assets.

Keep raw arrays: the old research pretty-printer folds one-element arrays into
objects and therefore cannot be used to count inputs. Never count a native
mapping tree's root children as physical inputs: a directional sequence is a
single child containing two separate stick motions.
"""
from __future__ import annotations
import csv
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / 'research/assets-json/Sifu/Content'
OUT = ROOT / 'customizer/data'


def value(properties, name, default=None):
    return next((p.get('Value', p.get('EnumValue')) for p in properties if p['Name'] == name), default)


def plain(item):
    if isinstance(item, dict):
        if 'Value' in item:
            return plain(item['Value'])
        if 'EnumValue' in item:
            return item['EnumValue']
        return {k: plain(v) for k, v in item.items() if not k.startswith('$')}
    if isinstance(item, list):
        if item and all(isinstance(p, dict) and 'Name' in p for p in item):
            return {p['Name']: plain(p) for p in item}
        return [plain(p) for p in item]
    return item


def tags(item):
    result = []
    def visit(x):
        if isinstance(x, dict):
            if x.get('Name') == 'TagName' and isinstance(x.get('Value'), str):
                result.append(x['Value'])
            for v in x.values():
                visit(v)
        elif isinstance(x, list):
            for v in x:
                visit(v)
    visit(item)
    return list(dict.fromkeys(result))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    with (ROOT / 'research/Game-zh-Hans.csv').open(encoding='utf-8-sig', newline='') as f:
        translations = {r['key']: r.get('source', '') for r in csv.DictReader(f)}
    input_table_path = ASSETS / 'Localization/StringTables/Menu/SkillTree/ST_UI_SkillTree_Inputs.json'
    input_table = json.loads(input_table_path.read_text(encoding='utf-8-sig'))
    input_fallback = dict(input_table['Exports'][0]['Table']['Value'])
    def text(key):
        # String-table namespaces vary; resolve only an unambiguous suffix.
        found = [v for k, v in translations.items() if k.rsplit('/', 1)[-1] == key]
        return found[0] if len(set(found)) == 1 else input_fallback.get(key, key)
    header = (ROOT / 'src/game_symbols.hpp').read_text(encoding='utf-8')
    action_ids = {name: int(number, 0) for name, number in re.findall(r'Action_(\w+)\s*=\s*(0x[\da-fA-F]+|\d+)', header)}
    originals = {
        'AttackLightBackFront': ['Back', 'Forward', 'X'],
        'AttackHeavyBackFront': ['Back', 'Forward', 'Y'],
        'AttackLightFrontFront': ['Forward', 'Forward', 'X'],
        'AttackHeavyFrontFront': ['Forward', 'Forward', 'Y'],
        'PushFromGrab': ['X', 'A'], 'DodgeAttack': ['Forward', 'RT'],
        'AttackLightHold': ['X'], 'AttackHeavyHold': ['Y'],
        'AttackHeavyGuard': ['LB', 'Y'], 'AttackLight': ['X'], 'AttackHeavy': ['Y'],
        'LightRushAttack': ['RT', 'X'], 'HeavyRushAttack': ['RT', 'Y'],
        'Disengage': ['X', 'Y'], 'ThrowContextualObject': ['RB'],
        'PushbackCancel': ['LB', 'Back'], 'ParryFromDown': ['LB'], 'Parry': ['LB'],
    }
    move_path = ASSETS / 'UI/Blueprints/Menus/Combos/BP_Menu_Combos.json'
    asset = json.loads(move_path.read_text(encoding='utf-8-sig'))
    moves = []
    for export in asset['Exports']:
        properties = export.get('Data', [])
        inputs = value(properties, 'ComboInput')
        if inputs is None:
            continue
        steps = []
        for entry in inputs:
            fields = {p['Name'].split('_', 1)[0]: p.get('Value', p.get('EnumValue')) for p in entry['Value']}
            action = fields['Action'].split('::')[-1]
            steps.append({'action': action, 'action_id': action_ids[action],
                          'hold': fields.get('NeedHold', False),
                          'movement': fields['Movement'].split('::')[-1],
                          'plus': fields.get('AdditionPictoAfterInput', False),
                          'arrow': fields.get('ArrowPictoAfterInput', False)})
        action = steps[0]['action']
        command = len(steps) == 1 and action not in ('AttackLight', 'AttackHeavy')
        count = len(originals[action]) if len(steps) == 1 else len(steps)
        original = originals[action] if len(steps) == 1 else [originals[s['action']][0] for s in steps]
        title_key = value(properties, 'Combo Name')
        # FText's Value is a string-table key; source is carried separately.
        moves.append({'id': export['ObjectName'], 'title_key': title_key, 'title_zh': text(title_key),
                      'kind': 'command' if command else 'chain' if len(steps) > 1 else 'conditional',
                      'original_step_count': count, 'original_inputs': original,
                      'steps': steps, 'skills': tags(next((p for p in properties if p['Name'] == 'SkillTagNeeded'), [])),
                      'variants': tags(next((p for p in properties if p['Name'] == 'AltMoveTags'), [])),
                      'condition_key': value(properties, 'Details Text'),
                      'source': move_path.relative_to(ROOT).as_posix() + '#' + export['ObjectName']})
        if export['ObjectName'] == 'YX-Disengage':moves[-1]['resolved_actions'] = ['Disengage']
    families = {}
    for move in moves:
        for step in move['steps']:
            families.setdefault(step['action'], []).append(move['id'])
    for move in moves:
        move['linked_moves'] = sorted({other for s in move['steps'] for other in families[s['action']] if other != move['id']})
    skills = []
    for path in sorted((ASSETS / 'DB/Skills').rglob('*.json')):
        if not any(x in path.parts for x in ('Techniques', 'Actions', 'Focus')):
            continue
        skill = json.loads(path.read_text(encoding='utf-8-sig'))
        ui = next((e for e in skill['Exports'] if any(p['Name'] == 'Inputs' for p in e.get('Data', []))), None)
        if ui is None:
            continue
        fields = plain(ui.get('Data', []))
        source = text(fields['Inputs'])
        referenced = re.findall(r'<action\s+id=[\"\'](\w+)[\"\']', source)
        skills.append({'id': path.stem, 'title_zh': text(fields['Name']), 'title_key': fields['Name'],
                       'input_text_key': fields['Inputs'], 'original_input_text': source,
                       'actions': referenced, 'skill_tags': tags(skill['Exports']),
                       'input_family': 'focus' if 'Focus' in path.parts else 'combat',
                       'original_step_count': sum(len(originals[a]) for a in referenced),
                       'source': path.relative_to(ROOT).as_posix()})
    # The internal row is named YX, but this gamepad profile actually maps X,Y.
    # Some other profiles reverse it. Never infer order from the row name or a
    # summary that combined several profiles.
    gameplay = json.loads((ASSETS / 'DB/Inputs/Profiles/DefaultGamepad/DefaultGamepad_Gameplay.json').read_text(encoding='utf-8-sig'))
    nodes = []
    def find_disengage(item):
        if isinstance(item, list):
            if len(item)==2 and isinstance(item[0], dict) and item[0].get('Value')=='InputAction::Disengage':
                nodes.append(item[1])
            for child in item:find_disengage(child)
        elif isinstance(item, dict):
            for child in item.values():find_disengage(child)
    find_disengage(gameplay)
    assert len(nodes)==1, len(nodes)
    slot_arrays=[]
    def find_slots(item):
        if isinstance(item, dict):
            if item.get('Name')=='m_Slots' and 'ArrayPropertyData' in item.get('$type',''):slot_arrays.append(item['Value'])
            for child in item.values():find_slots(child)
        elif isinstance(item, list):
            for child in item:find_slots(child)
    find_slots(nodes[0])
    assert len(slot_arrays)==1, len(slot_arrays)
    slots=[plain(s) for s in slot_arrays[0]]
    assert [s['m_PresetID']['m_Value'] for s in slots]==['InputPresets::NewEnumerator6','InputPresets::NewEnumerator8'], slots
    data = {'schema_version': 1, 'game_build': '18739835',
            'executable_sha256': '66845653ff78e4f1c31d7cfefccdcea435cbb5dc186451d443747b849c125e7e',
            'moves': moves, 'skills': skills,
            'sources': {'move_list_sha256': hashlib.sha256(move_path.read_bytes()).hexdigest(),
                        'skill_input_table_sha256': hashlib.sha256(input_table_path.read_bytes()).hexdigest()},
            'rules': {'count_origin': 'unmodified_assets', 'hold_counts_as_one_input': True,
                      'shared_native_actions_are_linked': True, 'unlock_checks_remain_native': True,
                      'focus_and_menu_are_native': True}}
    assert len(moves) == 40, f'Unexpected move catalogue: {len(moves)}'
    assert len({m['id'] for m in moves}) == len(moves)
    assert all(len(m['original_inputs']) == m['original_step_count'] for m in moves)
    destination = OUT / 'catalog.json'
    destination.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    # Small immutable catalogue for the in-game UI; no JSON parser or filesystem
    # reads on a drawing callback. Counts always come from unmodified sources.
    def quoted(value):
        return 'L' + json.dumps(value, ensure_ascii=False)
    entries=[]
    for move in moves:
        actions=move.get('resolved_actions', [s['action'] for s in move['steps']])
        note=text(move['condition_key']) if move['condition_key'] else ''
        note=re.sub(r'<[^>]*>', '', note).replace('\n', ' ')
        if move['kind']=='chain':note='普通连段，关联轻重攻击；原生节奏和招式条件保留。'
        elif move['kind']=='conditional':note='情境招式，关联基础攻击；'+note
        else:note=note or '保留原生解锁、距离和命中条件。'
        entries.append((move['title_zh'],actions,move['original_step_count'],False,False,any(s['hold'] for s in move['steps']),note))
    for skill in skills:
        focus=skill['input_family']=='focus'
        note='专注模式保持原生。' if focus else '技能解锁后生效；按确认查看关联改键。'
        entries.append(('技能 · '+skill['title_zh'],skill['actions'],skill['original_step_count'],True,focus,False,note))
    header=['#pragma once','#include <array>','#include <cstdint>','#include <cstddef>',
            'namespace button_controls::customizer {',
            'struct CatalogEntry {const wchar_t* title;std::array<uint8_t,5> actions;size_t count,operations;bool skill,focus,held;const wchar_t* note;};',
            f'inline constexpr std::array<CatalogEntry,{len(entries)}> catalog_entries = {{{{']
    for title,actions,count,skill,focus,held,note in entries:
        assert len(actions)<=5
        ids=','.join(str(action_ids[a]) for a in actions)
        header.append('    {'+quoted(title)+',{{'+ids+'}},'+str(len(actions))+','+str(count)+','+
                      ','.join(str(x).lower() for x in (skill,focus,held))+','+quoted(note)+'},')
    header+=['}};','}']
    (ROOT/'customizer/src/catalog_data.hpp').write_text('\n'.join(header)+'\n',encoding='utf-8')
    print(f'Catalogue: {len(moves)} move rows, {len(skills)} skills, {len(families)} native input families; {destination}')


if __name__ == '__main__':
    main()
