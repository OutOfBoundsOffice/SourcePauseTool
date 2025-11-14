# How to add a new feature
1. Copy and paste the spt/features/template.cpp.txt file
2. Rename to `yourfeature.cpp`
3. Add `yourfeature.cpp` to the `SPT_SOURCES` block in CMakeLists.txt 
4. Rename the default class and instance
5. Code
6. Auto-format your code with clang-format (you can use the script format.sh to do this automatically, can be run with Git Bash). In visual studio you can run clang-format with `Ctrl+K+D`.
7. Create a pull request

# Feature class virtual functions
The `Feature` base class provides some virtual functions and that you may override in your feature to do setup. There's some potential here to shoot yourself in the foot when you are trying to use things inside other features. Try to limit dependencies between features and when you have to depend on another feature, make sure:
1. The feature you are depending on is going to load
2. Anything you call will be ready for it. Do not rely on weird initialization order stuff, like "this feature always gets `LoadFeature` called before this one". Instead if you need something in `LoadFeature`, you could solve the issue by making sure the other feature initializes that in `PreHook`.

## `virtual bool ShouldLoadFeature()`
On plugin load, every feature is called to determine whether it should be loaded. Disable loading the feature in cases where you know it has no hope of working (e.g. Portal features should only load in Portal).

## `virtual void InitHooks()`
A function where you should init hooks and pattern finding stuff.

## `virtual void PreHook()`
A function called after pattern finding stuff has completed but SPT has not yet hooked anything. This place is used to mark signals as working, add hooks in some cases, or set some state that can be read by other features. For example the HUD feature sets `loadingSuccessful` depending on whether all the relevant patterns were found. This way features adding their HUD elements inside LoadFeature can be informed if the HUD element was successfully added.

## `virtual void LoadFeature()`  
This is for generic initialization code after hooking/pattern finding is done. Can:  
- Add HUD elements
- Init console commands/variables  

## `virtual void UnloadFeature()`
Called when the feature gets unloaded. Close any open resources like files or anything like that.

# Finding functions and patterns
There's some macros to help finding patterns and functions in feature.hpp.

## `HOOK_FUNCTION` - macro
Example:
```
// nosleep.cpp
	HOOK_FUNCTION(inputsystem, CInputSystem__SleepUntilInput);
```
```
// patterns.hpp:
namespace inputsystem
{
    PATTERNS(CInputSystem__SleepUntilInput,
                "5135",
                "8B 44 24 ?? 85 C0 7D ??",
                "5377866-BMS_Retail",
                "55 8B EC 8B 45 ?? 83 CA FF");
} // namespace inputsystem
```
`HOOK_FUNCTION` is a macro for hooking a function. This example hooks the function `CInputSystem::SleepUntilInput` in inputsystem.dll. The macro assumes that you have member called `ORIG_CInputSystem__SleepUntilInput` and a static function for the class called `HOOKED_CInputSystem__SleepUntilInput`.

After SPT has hooked the function the `HOOKED_CInputSystem__SleepUntilInput` is called instead of the original function and the original function can be called using the function pointer `ORIG_CInputSystem__SleepUntilInput`.

You should hook a function whe you want to alter the behavior of the function. If you don't need to alter the behavior, use `FIND_PATTERN`.

## `FIND_PATTERN` - macro
Example:
```
// hud.cpp
FIND_PATTERN(vguimatsurface, StartDrawing);
```
```
// patterns.hpp
...
    PATTERNS(
        StartDrawing,
        "5135",
        "55 8B EC 83 E4 C0 83 EC 38 80 ?? ?? ?? ?? ?? ?? 56 57 8B F9 75 57 8B ?? ?? ?? ?? ?? C6 ?? ?? ?? ?? ?? ?? FF ?? 8B 10 6A 00 8B C8 8B 42 20");
...
```
`FIND_PATTERN` finds a pointer to the address where the pattern is found. This pointer is placed into `ORIG_StartDrawing` in this case. This is typically used for finding a function and then the pointer can be used to call the function.

## `DECL_HOOK_THISCALL`
`HOOK_FUNCTION` assumes that you have certain functions and members declared and defined, which can be cumbersome to write manually. To help with this, feature.hpp has the macro `DECL_HOOK_THISCALL`.
Example:
```
// nosleep.cpp
class NoSleepFeature : public FeatureWrapper<NoSleepFeature>
{
...
private:
	DECL_HOOK_THISCALL(void, CInputSystem__SleepUntilInput, int nMaxSleepTimeMS);
};
```
which declares the function pointer `ORIG_CInputSystem__SleepUntilInput` and the corresponding static function `HOOKED_CInputSystem__SleepUntilInput` that takes in one argument, `int nMaxSleepTimeMS`. The fully expanded macro looks as follows:
```
	using _CInputSystem__SleepUntilInput = void(__fastcall*)(void* thisptr, int edx, int nMaxSleepTimeMS);
	_CInputSystem__SleepUntilInput ORIG_CInputSystem__SleepUntilInput = nullptr;
	static void __fastcall HOOKED_CInputSystem__SleepUntilInput(void* thisptr, int edx, int nMaxSleepTimeMS);
```
The first line declares the type of the function pointer with the "using" statement. The second line declares the member and the last line declares the function. The first two arguments to the hooked function thisptr and edx are implicit arguments for a member function call. Thisptr is a pointer to the class instance. The edx argument seems to be unused.

## `DECL_HOOK_CDECL`
`DECL_HOOK_THISCALL` is only for hooking member functions, which are 95% of the functions you need to hook. `DECL_HOOK_CDECL` can be used to declare hooks for normal functions. The only difference between these macros is that the calling convention is different and this macro does not declare the implicit thisptr and edx arguments.

## `DECL_MEMBER_CDECL` and `DECL_MEMBER_THISCALL`
These macros are the same as the above but they do not declare the function itself. These should be used when you only intend to call the function but do not want to change how it works.

## `HOOK_THISCALL`
A function declaration needs a matching definition to do anything. To help with writing the function signature, you can use the macro `HOOK_THISCALL`. Example:
```
HOOK_THISCALL(void, NoSleepFeature, CInputSystem__SleepUntilInput, int nMaxSleepTimeMS)
{
	if (y_spt_focus_nosleep.GetBool())
		nMaxSleepTimeMS = 0;
	spt_nosleep.ORIG_CInputSystem__SleepUntilInput(thisptr, edx, nMaxSleepTimeMS);
}
```
As you might have guessed, the macro `HOOK_CDECL` can be used to write the function signature for normal function hooks. Example:
```
HOOK_CDECL(void, DemoStuff, Stop)
{
	if (spt_demostuff.ORIG_Stop)
	{
		spt_demostuff.isAutoRecordingDemo = false;
		spt_demostuff.currentAutoRecordDemoNumber = 1;
		spt_demostuff.ORIG_Stop();
	}
}
```

## What pattern found my function?
In some cases you have multiple patterns under the same name to support multiple versions of the game. You might also want to do some pattern specific setup, to account for differences between versions. To determine the pattern used, you can use the function GetPatternIndex. Example:
```
// patterns.hpp:
PATTERNS(
    SpawnPlayer,
    "5135",
    "83 EC 14 80 3D ?? ?? ?? ?? 00 56 8B F1 74 ?? 6A 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? EB 23 8B 0D ?? ?? ?? ?? 8B 01 8B 96 ?? ?? ?? ?? 8B 40 0C 52 FF D0 8B 8E ?? ?? ?? ?? 51 E8 ?? ?? ?? ?? 83 C4 04 8B 46 0C 8B 56 04 8B 52 74 83 C0 01",
    "5339",
    "55 8B EC 83 EC 14 80 3D ?? ?? ?? ?? 00 56 8B F1 74 ?? 6A 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? EB 23 8B 0D ?? ?? ?? ?? 8B 01 8B 96 ?? ?? ?? ?? 8B 40 0C 52 FF D0 8B 8E ?? ?? ?? ?? 51 E8 ?? ?? ?? ?? 83 C4 04 8B 46 0C 8B 56 04 8B 52 74 40",
...
```
```
// pause.cpp
	if (ORIG_SpawnPlayer)
	{
		int ptnNumber = GetPatternIndex((void**)&ORIG_SpawnPlayer);

		switch (ptnNumber)
		{
		case 0:
			pM_bLoadgame = (*(bool**)(ORIG_SpawnPlayer + 5));
			pGameServer = (*(void**)(ORIG_SpawnPlayer + 18));
			break;
...
```
Here the 0 index corresponds to the 5135 pattern, 1 to 5339 and so on.

# Initializing console commands/variables
Console commands and variables have to be initialized for SPT to load them. Otherwise they get automatically unloaded. Example:
```
// ipc-spt.cpp
void ipc::IPCFeature::LoadFeature()
{
	if (FrameSignal.Works)
	{
		Init();
		FrameSignal.Connect(Loop);
#ifndef OE
		InitCommand(y_spt_ipc_ent);
		InitCommand(y_spt_ipc_properties);
#endif
		InitCommand(y_spt_ipc_echo);
		InitCommand(y_spt_ipc_playback);
		InitCommand(y_spt_ipc_gamedir);

		InitConcommandBase(y_spt_ipc);
		InitConcommandBase(y_spt_ipc_port);
	}
}
```
The `FrameSignal.Works` check checks if `FrameSignal` has been initialized properly. If it is, then all the relevant commands are initialized with the macro `InitCommand` and variables are initialized with the command `InitConcommandBase`.

Check that everything the console variable/command needs has been successfully loaded. Ideally every SPT command/variable that loads works at least partially. If the command/variable does nothing OR crashes when it is used, it should not be initialized.