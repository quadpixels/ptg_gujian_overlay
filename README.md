This is a sample project that makes use of Supermouse's Gujian MOD SDK. It produces `ptg_overlay.dll` that adds a few features to Gujian:

* Fixes mouse speed while the character is moving (Only applicable to Steam version)
* Shows original text for English MODs
  * Note: `ptg_overlay.dll` must be re-compiled with resource files generated for its corresponding `data.table` for the original text to be displayed properly!

> Disclaimer:
>
> Gujian intellectural property belongs to Wangyuan Shengtang

Prerequisites: 
 - Microsoft Visual Studio (I used 2019)
 - [Microsoft DirectX SDK (June 2010)](https://www.microsoft.com/en-in/download/details.aspx?id=6812)

Build notes:
 - Update the VC++ include and library directories (Project -> Properties -> VC++ Directories)
 - Add "d3d9x.lib" to (Project -> Properties -> Linker -> Input -> Additional Dependencies)
 - Update the path to the DEF file at: (Project -> Properties -> Linker -> Input -> Module Definition File)

Update Log:
- 2018-06-03: Figuring out how to set the "DEF File" and using the MOD SDK's header file
- 2018-06-10: Hooking the `IDirect3DDevice9::Present` method
- 2018-08-21: Testbed for running the DLL without having to start the game
- 2024-03-24: Small fixes, disabled redundant detection code

![十分美妙](./screenshot.gif)

(:
