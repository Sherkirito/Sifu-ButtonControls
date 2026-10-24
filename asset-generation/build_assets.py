"""Build a small overlay pak from the exact installed, locally exported assets.

No original pak or save files are edited. Menu/focus profiles are deliberately
excluded. Runtime input and the UI's native mapping trees share action IDs.
"""
from __future__ import annotations
import copy
import csv
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'research/assets-json/Sifu/Content'
OUT = ROOT / 'customizer/build/assets-json/Sifu/Content'
PAK_ROOT = ROOT / 'customizer/build/pak'
MOD = 'Mods/ButtonControls/Inputs'
created = {}
manifest = []


def load(path):
    return json.loads((SOURCE / path).with_suffix('.json').read_text(encoding='utf-8-sig'))


def prop(values, name):
    return next(item for item in values if item.get('Name') == name)


def node(name, kind, value, **extra):
    return {'$type':f'UAssetAPI.PropertyTypes.Objects.{kind}PropertyData, UAssetAPI',
            'Name':name,'ArrayIndex':0,'PropertyGuid':None,'IsZero':False,
            'PropertyTagFlags':'None','PropertyTypeName':None,
            'PropertyTagExtensions':'NoExtension','Value':value,**extra}


def set_prop(asset, name, kind, value, **extra):
    values = asset['Exports'][0]['Data']
    replacement = node(name,kind,value,**extra)
    for index, item in enumerate(values):
        if item['Name'] == name:
            values[index] = replacement
            return
    values.append(replacement)


def clone(template, name):
    asset = load(template)
    old_name = asset['Exports'][0]['ObjectName']
    old_package = '/Game/' + str(Path(template).with_suffix('')).replace('\\','/')
    new_package = f'/Game/{MOD}/{name}'
    asset['NameMap'] = [new_package if item == old_package else name if item == old_name else item for item in asset['NameMap']]
    asset['Exports'][0]['ObjectName'] = name
    for imp in asset.get('Imports',[]):
        if imp['ClassName']=='Package' and imp['ObjectName']==old_package:imp['ObjectName']=new_package
    return asset


def reference(asset, package, class_name, object_name=None):
    object_name = object_name or package.rsplit('/',1)[-1]
    imports = asset['Imports']
    for index, item in enumerate(imports):
        if item['ObjectName']==object_name and item['ClassName']==class_name:return -index-1
    package_index = next((-i-1 for i,v in enumerate(imports) if v['ClassName']=='Package' and v['ObjectName']==package),None)
    if package_index is None:
        imports.append({'$type':'UAssetAPI.Import, UAssetAPI','ObjectName':package,'OuterIndex':0,
                        'ClassPackage':'/Script/CoreUObject','ClassName':'Package','PackageName':None,'bImportOptional':False})
        package_index = -len(imports)
    imports.append({'$type':'UAssetAPI.Import, UAssetAPI','ObjectName':object_name,'OuterIndex':package_index,
                    'ClassPackage':'/Script/Sifu','ClassName':class_name,'PackageName':None,'bImportOptional':False})
    return -len(imports)


def collect_names(value, names):
    if isinstance(value,dict):
        property_type=value.get('$type','').split(',')[0].rsplit('.',1)[-1]
        if property_type.endswith('PropertyData'):names.add(property_type[:-4])
        if 'Name' in value and isinstance(value['Name'],str):names.add(value['Name'])
        if 'StructType' in value and value['StructType']:names.add(value['StructType'])
        if 'EnumType' in value and value['EnumType']:names.add(value['EnumType'])
        if value.get('EnumValue'):names.add(value['EnumValue'])
        if value.get('$type','').endswith(('NamePropertyData, UAssetAPI','EnumPropertyData, UAssetAPI')) and isinstance(value.get('Value'),str):names.add(value['Value'])
        for item in value.values():collect_names(item,names)
    elif isinstance(value,list):
        for item in value:collect_names(item,names)


def write(path, asset, reason):
    names=set(asset['NameMap'])
    collect_names(asset,names)
    for imp in asset.get('Imports',[]):
        names.update((imp['ObjectName'],imp['ClassName'],imp['ClassPackage']))
    for exp in asset['Exports']:names.add(exp['ObjectName'])
    asset['NameMap'].extend(sorted(names-set(asset['NameMap'])))
    target=(OUT/path).with_suffix('.json')
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(json.dumps(asset,ensure_ascii=False,indent=2),encoding='utf-8')
    created[path]=target
    manifest.append({'path':'Sifu/Content/'+str(Path(path).with_suffix('.uasset')).replace('\\','/'),'reason':reason})


def enum_array(name, actions):
    return node(name,'Array',[node(str(i),'Enum','InputAction::'+action,EnumType=None,InnerType=None) for i,action in enumerate(actions)],ArrayType='EnumProperty')


def input_data(name, behavior, hold=0, hold_icon=False, maintain=False, action='InputAction_MAX'):
    template=('DB/Generic/Maintain/RushMaintainHoldInput' if maintain else
              'DB/Generic/GenericLongHoldInput' if hold_icon else 'DB/Generic/GenericJustPressInput')
    asset=clone(template,name)
    values=asset['Exports'][0]['Data']
    behavior_struct=prop(values,'m_BehaviourStruct')
    behavior_struct['Value']=[node('m_behavior','Enum','InputBehavior::'+behavior,EnumType='InputBehavior',InnerType=None),
                             node('m_fTimePressed','Float',hold),node('m_bAutoReset','Bool',not maintain)]
    set_prop(asset,'m_bIsToggleable','Bool',False)
    set_prop(asset,'m_action','Enum','InputAction::'+action,EnumType='InputAction',InnerType=None)
    write(f'{MOD}/{name}',asset,'Controller-only input primitive with explicit press/hold behavior')
    return name


def sequence(name, action, count):
    asset=clone('DB/Generic/GenericInputSequence',name)
    data=asset['Exports'][0]['Data']
    steps=prop(data,'m_Sequence')
    prototype=copy.deepcopy(steps['Value'][0])
    steps['Value']=[]
    primitive=reference(asset,f'/Game/{MOD}/BC_Press','InputData')
    for index in range(count):
        step=copy.deepcopy(prototype)
        prop(step['Value'],'m_Input')['Value']=primitive
        interval=prop(step['Value'],'m_DelayBeforeNextAction')['Value']
        upper=prop(interval,'UpperBound')['Value']
        prop(upper,'Type')['EnumValue']='ERangeBoundTypes::Inclusive'
        prop(upper,'Value')['Value']=0.4
        steps['Value'].append(step)
    set_prop(asset,'m_action','Enum','InputAction::'+action,EnumType='InputAction',InnerType=None)
    set_prop(asset,'m_bResetOnAnyActionStacked','Bool',True)
    set_prop(asset,'m_UISeparator','Str',' → ')
    reset=enum_array('m_ResetOnEnterStack',['AttackKnockdownHold','AttackLight','AttackLightAlt','AttackLightHold','AttackHeavy','AttackHeavyAlt','AttackHeavyHold','PushFromGrab','CounterGrab'])
    data.append(reset)
    write(f'{MOD}/{name}',asset,'Native button sequence mapping also supplies the displayed sequence')


def combination(name,action=None):
    asset=clone('DB/Generic/Combinations/GenericCombinationInput',name)
    data=asset['Exports'][0]['Data']
    primitive=reference(asset,f'/Game/{MOD}/BC_Press','InputData')
    for item in prop(data,'m_IncludedInputs')['Value']:
        prop(item['Value'],'m_Input')['Value']=primitive
    if action:
        set_prop(asset,'m_action','Enum','InputAction::'+action,EnumType='InputAction',InnerType=None)
        set_prop(asset,'m_bIsStackable','Bool',False)
    set_prop(asset,'m_UISeparator','Str',' + ')
    write(f'{MOD}/{name}',asset,'Button chord display; runtime selects its gameplay action')


def profile_mappings(profile):
    groups=prop(profile['Exports'][0]['Data'],'m_MappingGroups')['Value']
    for group in groups:
        mapping=prop(group['Value'],'m_Mapping')['Value']
        for pair in mapping:yield pair,prop(pair[1]['Value'],'m_MappingData')


def build_profile():
    path='DB/Inputs/Profiles/DefaultGamepad/DefaultGamepad_Gameplay'
    profile=load(path)
    mappings={pair[0]['Value'].split('::')[-1]:(pair,mapping) for pair,mapping in profile_mappings(profile)}
    # Use a direct single-key slot that has no dependency on UI accept/back presets.
    slot=copy.deepcopy(prop(mappings['DialogSentence0'][1]['Value'],'m_Slots')['Value'][0])
    action_template=copy.deepcopy(prop(prop(slot['Value'],'m_Key')['Value'],'m_Actions')['Value'][0])
    def key_slot(key):
        result=copy.deepcopy(slot)
        preset=prop(result['Value'],'m_PresetID')
        prop(preset['Value'],'m_Value')['Value']='None'
        prop(result['Value'],'m_SubInputData')['Value']=0
        key_data=prop(result['Value'],'m_Key')['Value']
        prop(key_data,'m_eVirtualAction')['Value']='EVirtualAction::None'
        prop(key_data,'m_Axis')['Value']=[]
        action=copy.deepcopy(action_template)
        prop(action['Value'],'ActionName')['Value']='None'
        prop(prop(action['Value'],'Key')['Value'],'KeyName')['Value']=key
        prop(key_data,'m_Actions')['Value']=[action]
        return result
    def mapping(action,data_name,keys,class_name='InputData'):
        if action in mappings:
            pair, target=mappings[action]
        else:
            source_pair,source_target=mappings['PushbackCancel']
            pair=copy.deepcopy(source_pair)
            pair[0]['Value']='InputAction::'+action
            target=prop(pair[1]['Value'],'m_MappingData')
            groups=prop(profile['Exports'][0]['Data'],'m_MappingGroups')['Value']
            extra=copy.deepcopy(groups[-1])
            prop(extra['Value'],'m_Mapping')['Value']=[pair]
            groups.append(extra)
        values=target['Value']
        prop(prop(values,'m_PresetID')['Value'],'m_Value')['Value']='None'
        prop(values,'m_InputData')['Value']=reference(profile,f'/Game/{MOD}/{data_name}',class_name)
        prop(values,'m_Slots')['Value']=[key_slot(key) for key in keys]
    A='Gamepad_FaceButton_Bottom';B='Gamepad_FaceButton_Right';X='Gamepad_FaceButton_Left';Y='Gamepad_FaceButton_Top'
    LB='Gamepad_LeftShoulder';RB='Gamepad_RightShoulder'
    for action,name,keys in [('AttackLightBackFront','BC_Palm',[B,A,X]),('AttackHeavyBackFront','BC_Sweep',[A,B,Y]),
                              ('AttackLightFrontFront','BC_Crotch',[B,B,X]),('AttackHeavyFrontFront','BC_Kick',[B,B,Y])]:
        mapping(action,name,keys,'InputSequenceData')
    for action in ['Rush','Dodge']:
        _,target=mappings[action]
        original=profile['Imports'][-prop(target['Value'],'m_InputData')['Value']-1]
        # Preserve the distinct stock dash/rush timing primitives.
        primitive='BC_Rush' if action=='Rush' else 'BC_Press'
        mapping(action,primitive,['Gamepad_LeftThumbstick'])
    mapping('Action','BC_Press',['Gamepad_RightThumbstick'])
    mapping('StopTraining','BC_TrainingHold',['Gamepad_RightThumbstick'])
    # The optional Bullet Time modifier also occupied A. Give it a contextual
    # combat binding while retaining its native maintain/hold behavior.
    _,bullet=mappings['BulletTime']
    prop(prop(bullet['Value'],'m_PresetID')['Value'],'m_Value')['Value']='None'
    prop(bullet['Value'],'m_Slots')['Value']=[key_slot('Gamepad_DPad_Left')]
    # Traversal retains its existing interchange/hold graph; replace only its preset slots.
    _,traversal=mappings['Traversal']
    prop(prop(traversal['Value'],'m_PresetID')['Value'],'m_Value')['Value']='None'
    prop(traversal['Value'],'m_Slots')['Value']=[key_slot('Gamepad_RightThumbstick'),key_slot('Gamepad_RightThumbstick')]
    mapping('TakedownValidation','BC_Press',['Gamepad_RightTrigger'])
    mapping('AttackKnockdownHold','BC_GroundHold',[B])
    for action in ['PickUpObject','ContextualActionObject','DropObject']:
        mapping(action,'BC_RBHold',[RB])
    for action in ['ThrowObject','ThrowFromPickUp','ThrowContextualObject']:
        mapping(action,'BC_RBRelease',[RB])
    mapping('Avoid','BC_Chord',[LB,A],'BehaviourCombinationInputData')
    mapping('PushbackCancel','BC_Chord',[LB,A],'BehaviourCombinationInputData')
    # UI-only alias, never enabled as a gameplay attack by the runtime.
    mapping('AttackHeavyFront','BC_AvoidLowUI',[LB,B],'BehaviourCombinationInputData')
    # DodgeAttack keeps its directional node; bind its dash child directly to L3.
    _,invert=mappings['DodgeAttack']
    slots=prop(invert['Value'],'m_Slots')['Value']
    slots[0]=key_slot('Gamepad_LeftThumbstick')
    # FlyMode conflicts with the newly occupied stick clicks. Its gameplay entry is removed.
    for group in prop(profile['Exports'][0]['Data'],'m_MappingGroups')['Value']:
        entries=prop(group['Value'],'m_Mapping')['Value']
        entries[:]=[pair for pair in entries if pair[0]['Value']!='InputAction::FlyMode']
    write(path,profile,'Gameplay-only Xbox profile. Menu and focus profiles remain byte-for-byte original.')


def build_profile_contexts():
    path='DB/Inputs/Profiles/DefaultGamepad/DefaultGamepad'
    asset=load(path)
    profile=next(e for e in asset['Exports'] if e['ObjectName']=='DefaultGamepad')
    mappings=prop(profile['Data'],'m_MappingPerContext')['Value']
    assert not any(pair[0]['Value']=='InputContext::BulletTime' for pair in mappings)
    gameplay=next(pair for pair in mappings if pair[0]['Value']=='InputContext::BasicGameplay')
    bullet=copy.deepcopy(gameplay)
    bullet[0]['Value']='InputContext::BulletTime'
    mappings.append(bullet)
    write(path,asset,'Bullet Time uses the same modified combat profile; all existing context routes and profile UI data are preserved.')


def build_move_list():
    path='UI/Blueprints/Menus/Combos/BP_Menu_Combos'
    asset=load(path)
    changed=[]
    for exp in asset['Exports']:
        if exp['ObjectName'] not in {'BFX-PalmAttack','BFY-SweepKick','FFX-GroundedPunch','FFY-ThrustKick'}:continue
        for value in exp.get('Data',[]):
            if value['Name']=='ComboInput':
                for entry in value['Value']:
                    for p in entry.get('Value',[]):
                        if p['Name'].startswith('Movement_'):
                            p['EnumValue' if 'EnumValue' in p else 'Value']='EControllerIconAxisTypes::None'
                changed.append(exp['ObjectName'])
    write(path,asset,'Remove extra hard-coded stick pictograms from four remapped command rows; native action graphs supply BAX/ABY/BB icons.')
    return changed


def build_tutorial_inputs():
    path='Blueprints/Tutorial/HUD/Widgets/Input/DT_TutorialPlayerInputs'
    asset=load(path)
    changed=[]
    remapped={'PalmStrike','LowKick','GroundedStraightPunch','JumpingHighKick','AvoidDuck','AvoidSide','AvoidJump'}
    for row in asset['Exports'][0]['Table']['Data']:
        if row['Name'] not in remapped:continue
        inputs=next(p for p in row['Value'] if p['Name'].startswith('InputDatas_'))
        for item in inputs['Value']:
            for p in item['Value']:
                if p['Name'].startswith('Movement_'):p['Value']='EControllerIconAxisTypes::None'
                if row['Name'].startswith('Avoid'):
                    if p['Name'].startswith('NeedHold_'):p['Value']=False
                    if p['Name'].startswith('Action_'):
                        p['Value']='InputAction::'+('AttackHeavyFront' if row['Name']=='AvoidJump' else 'Avoid')
        changed.append(row['Name'])
    write(path,asset,'Seven tutorial input rows use button graphs instead of fixed stick arrows; low/high avoid receive distinct glyphs.')
    return changed


def build_ui_alias_context():
    for context in ['BasicGameplay','BulletTime']:
        path='DB/Inputs/Contexts/'+context
        asset=load(path)
        data=asset['Exports'][0]['Data']
        inputs=prop(data,'m_inputActions')['Value']
        settings=prop(data,'m_InputActionSettings')['Value']
        for name,action,kind in [('BC_AvoidLowUI','AttackHeavyFront','BehaviourCombinationInputData'),
                                 ('BC_TrainingHold','StopTraining','InputData')]:
            new_input=copy.deepcopy(inputs[0])
            new_input['Name']=str(len(inputs))
            new_input['Value']=reference(asset,f'/Game/{MOD}/{name}',kind)
            inputs.append(new_input)
            alias=copy.deepcopy(settings[0])
            alias[0]['Value']='InputAction::'+action
            settings.append(alias)
        write(path,asset,'Register non-stackable low-avoid and training-exit display aliases in both combat contexts.')


def build_training_exit():
    path='Blueprints/TrainingRoom/BP_HUD_TrainingRoom'
    asset=load(path)
    changed=[]
    for exp in asset['Exports']:
        if exp['ObjectName'] not in {'Hint_Quit','Hint_Quit_Tuto'}:continue
        mapping=prop(exp['Data'],'Mapping ID')['Value']
        assert prop(mapping,'m_eAction')['Value']=='InputAction::SkipCinematic'
        prop(mapping,'m_eAction')['Value']='InputAction::StopTraining'
        changed.append(exp['ObjectName'])
    assert len(changed)==2
    write(path,asset,'Training exit hints display the separate R3 hold. Keyboard icons fall back to the original skip mapping at runtime; native map bytecode is preserved.')


def build_modifier_move_list():
    path='UI/Blueprints/Menus/Options/BP_Menu_Options_Cheat'
    asset=load(path)
    for exp in asset['Exports']:
        for p in exp.get('Data',[]):
            if p['Name']!='MovesetTitleComboInput':continue
            for item in p['Value']:
                action=next((v['Value'] for v in item['Value'] if v['Name'].startswith('Action_')),None)
                if action not in {'InputAction::'+a for a in ['AttackLightBackFront','AttackHeavyBackFront','AttackLightFrontFront','AttackHeavyFrontFront']}:continue
                for v in item['Value']:
                    if v['Name'].startswith('Movement_'):v['Value']='EControllerIconAxisTypes::None'
    write(path,asset,'Update the modifier/moveset preview command glyphs as well as the main move list.')


def build_controller_legend():
    path='UI/Blueprints/Menus/Options/BP_Menu_Options_Remapping_Layout'
    asset=load(path)
    labels={
        'XBOX_Move':('移动；L3 腾挪 / 长按奔跑',False),
        'XBOX_Camera':('视角；R3 互动 / 翻越',False),
        'XBOX_Dodge':('终结',False),
        'XBOX_Run':('',False),
        'XBOX_InteractVault':('招式前缀 A',False),
        'XBOX_PickUpWeapon':('招式前缀 B',False),
        'XBOX_ThrowWeapon':('短按：环境攻击 / 投掷',False),
        'XBOX_Guard':('格挡；+ A 闪高 / + B 闪低',False),
    }
    count=0
    for exp in asset['Exports']:
        base=exp['ObjectName'].rsplit('_',1)[0] if exp['ObjectName'].rsplit('_',1)[-1].isdigit() else exp['ObjectName']
        if base not in labels:continue
        values=exp.get('Data',[])
        action_type=next((p.get('EnumValue') for p in values if p['Name']=='ActionType'),None)
        # Alternate move/camera labels describe focus navigation; retain them.
        if base=='XBOX_Move' and action_type!='BP_RemappingActionTypes::NewEnumerator10':continue
        if base=='XBOX_Camera' and action_type!='BP_RemappingActionTypes::NewEnumerator11':continue
        value,hold=labels[base]
        if base=='XBOX_ThrowWeapon' and action_type=='BP_RemappingActionTypes::NewEnumerator19':value,hold='长按：拾取 / 丢弃',True
        text=prop(values,'ActionText')
        text.update(HistoryType='None',Flags=2,TableId=None,Namespace=None,Value=None,CultureInvariantString=value)
        old_hold=next((p for p in values if p['Name']=='DisplayHold'),None)
        if old_hold:old_hold['Value']=hold
        elif hold:values.append(node('DisplayHold','Bool',True))
        count+=1
    write(path,asset,'Xbox controller legend describes the remapped gameplay buttons; focus-specific labels and menu navigation are retained.')
    return count


def build_localization():
    changes={
        'UI_Lessons/Avoid_InGame_Step1':'手柄：<action id="Avoid"/>，闪避高位和中位攻击。可在暂停菜单按 R3 自定义。键盘保持原操作。',
        'UI_Lessons/Avoid_InGame_Step4':'手柄：<action id="AttackHeavyFront"/>，闪避低位攻击。可在暂停菜单按 R3 自定义。键盘保持原操作。',
        'UI_Lessons/Weapons_InGame_Step7':'手持物品时，<action id="ThrowFromPickUp"/>（短按后松手）以<FocusBlack>投掷</>；<action id="DropObject"/>以丢弃。键盘保持原操作。',
        'SkillTree/SkillInput_Action_InstantThrow':'<action id="ThrowObject"/> <LegendWhite>空手接近物品时短按，松手触发；需解锁环境掌控</>',
        'UI_Cheats/MODIFIER_BulletTime_Description':'启用此修改器后，战斗中按住方向键 ← 减缓时间流速；训练和对话选择时不启用。',
    }
    csv_path=ROOT/'customizer/build/Game-zh-Hans-modified.csv'
    with (ROOT/'research/Game-zh-Hans.csv').open(encoding='utf-8-sig',newline='') as source:
        rows=list(csv.DictReader(source))
    for row in rows:
        row['target']=changes.get(row['key'],'')
    assert set(changes).issubset({row['key'] for row in rows})
    with csv_path.open('w',encoding='utf-8-sig',newline='') as target:
        writer=csv.DictWriter(target,fieldnames=['key','source','target'])
        writer.writeheader();writer.writerows(rows)
    source=ROOT/'research/original/Sifu/Content/Localization/Game/zh-Hans/Game.locres'
    target=PAK_ROOT/'Sifu/Content/Localization/Game/zh-Hans/Game.locres'
    target.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run([str(ROOT/'tools/UnrealLocres.exe'),'import',str(source),str(csv_path),'-o',str(target)],capture_output=True,check=True,creationflags=0x08000000)
    manifest.append({'path':target.relative_to(PAK_ROOT).as_posix(),'reason':'Five Simplified Chinese instruction strings updated; all other localization entries preserved.','keys':list(changes)})
    return changes


def serialize(item):
    path,source=item
    target=(PAK_ROOT/'Sifu/Content'/path).with_suffix('.uasset')
    target.parent.mkdir(parents=True,exist_ok=True)
    completed=subprocess.run([str(ROOT/'tools/UAssetGUI.exe'),'fromjson',str(source),str(target)],capture_output=True,creationflags=0x08000000,timeout=90)
    if completed.returncode or not target.exists():raise RuntimeError(f'Failed to serialize {path}: exit={completed.returncode}, stdout={completed.stdout!r}, stderr={completed.stderr!r}')
    return str(target)


def main():
    input_data('BC_Press','JustPressed')
    input_data('BC_Rush','Pressed',0.16,True,maintain=True)
    input_data('BC_GroundHold','Pressed',0.30,True)
    input_data('BC_RBHold','Pressed',0.30,True)
    input_data('BC_RBRelease','JustReleasedBefore',0.30)
    input_data('BC_TrainingHold','Pressed',1.0,True,maintain=True,action='StopTraining')
    for name,action in [('BC_Palm','AttackLightBackFront'),('BC_Sweep','AttackHeavyBackFront'),('BC_Crotch','AttackLightFrontFront'),('BC_Kick','AttackHeavyFrontFront')]:sequence(name,action,3)
    combination('BC_Chord')
    combination('BC_AvoidLowUI','AttackHeavyFront')
    build_profile()
    build_profile_contexts()
    changed=build_move_list()
    tutorial_rows=build_tutorial_inputs()
    build_ui_alias_context()
    build_training_exit()
    build_modifier_move_list()
    legend_labels=build_controller_legend()
    with ThreadPoolExecutor(max_workers=4) as executor:list(executor.map(serialize,created.items()))
    localized=build_localization()
    (ROOT/'customizer/build/replacement-manifest.json').write_text(json.dumps({'assets':manifest,'move_list_rows':changed,'tutorial_rows':tutorial_rows,'legend_labels':legend_labels,'localized':localized},ensure_ascii=False,indent=2),encoding='utf-8')
    subprocess.run([str(ROOT/'tools/repak/repak.exe'),'pack',str(PAK_ROOT),str(ROOT/'customizer/build/ButtonControls_P.pak')],check=True,capture_output=True)
    print(f'Built {len(created)} assets, {len(changed)} move-list rows, pak={ROOT / "customizer/build/ButtonControls_P.pak"}')


if __name__=='__main__':main()
