# Workaround: Arduino-ESP32 3.x's USB library depends on FS (library.properties
# "depends=FS"), but PlatformIO does not add FS's include dir when compiling
# USBHostMSC.cpp/USBMSCFS.cpp, so the build fails with "FS.h: No such file".
# Add it explicitly, resolved from the installed framework package.
import os

Import("env")  # noqa: F821 (SCons builtin)

fw = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
if fw:
    env.Append(CPPPATH=[os.path.join(fw, "libraries", "FS", "src")])
