The .cs file in this folder is a dynamically-compiled build task. No .dll is checked in with this build task compiled into it; at build time, MSBuild compiles it just before it is used.
You can make changes to the .cs file and they will immediately affect the build -- it is a 1-step editing process.

The provided .csproj file in this folder is not in the build. It exists only to provide a nice editing environment for any engineer who needs to modify this build task.
If you build the project, it will create a .dll. We have no use for this compiled .dll, so please don't check it in. Just make your changes in Visual Studio, save the .cs file, and run the build again.