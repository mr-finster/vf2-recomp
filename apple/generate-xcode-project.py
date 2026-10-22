#!/usr/bin/env python3
"""Regenerate the checked-in, relocatable Xcode project; no third-party modules."""
import hashlib
import json
import subprocess
from pathlib import Path
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parent.parent
PROJECT = ROOT / "VF2Recomp.xcodeproj"
objects = {}
existing_objects = {}
if (PROJECT / "project.pbxproj").exists():
    existing_objects = json.loads(subprocess.check_output([
        "plutil", "-convert", "json", "-o", "-", str(PROJECT / "project.pbxproj")
    ]))["objects"]


def uid(key):
    return hashlib.sha256(key.encode()).hexdigest()[:24].upper()


def obj(key, isa, **values):
    identifier = uid(key)
    assert identifier not in objects, key
    objects[identifier] = {"isa": isa, **values}
    return identifier


def serialize(value, indent=0):
    if isinstance(value, dict):
        lines = ["{"]
        for key, item in value.items():
            lines.append("\t" * (indent + 1) + f"{key} = {serialize(item, indent + 1)};")
        lines.append("\t" * indent + "}")
        return "\n".join(lines)
    if isinstance(value, list):
        return "(" + ", ".join(serialize(item, indent) for item in value) + ")"
    if isinstance(value, int):
        return str(value)
    return json.dumps(str(value), ensure_ascii=False)


groups = {"": obj("group:root", "PBXGroup", children=[], sourceTree="<group>")}
files = {}


def group(path):
    if path not in groups:
        parent = str(Path(path).parent)
        parent = "" if parent == "." else parent
        parent_id = group(parent)
        groups[path] = obj("group:" + path, "PBXGroup", name=Path(path).name,
                           children=[], sourceTree="<group>")
        objects[parent_id]["children"].append(groups[path])
    return groups[path]


def file(path, kind=None):
    if path not in files:
        suffixes = {".cpp": "sourcecode.cpp.cpp", ".c": "sourcecode.c.c",
                    ".mm": "sourcecode.cpp.objcpp", ".h": "sourcecode.c.h",
                    ".plist": "text.plist.xml", ".py": "text.script.python",
                    ".sh": "text.script.sh", ".json": "text.json"}
        files[path] = obj("file:" + path, "PBXFileReference", path=path,
                          name=Path(path).name, sourceTree="SOURCE_ROOT",
                          lastKnownFileType=kind or suffixes.get(Path(path).suffix, "text"))
        parent = str(Path(path).parent)
        objects[group("" if parent == "." else parent)]["children"].append(files[path])
    return files[path]


def configs(key, settings):
    identifiers = []
    for name in ("Debug", "Release"):
        values = dict(settings)
        values.update({"GCC_OPTIMIZATION_LEVEL": "2" if name == "Debug" else "3",
                       "DEBUG_INFORMATION_FORMAT": "dwarf" if name == "Debug" else "dwarf-with-dsym",
                       "ONLY_ACTIVE_ARCH": "YES" if name == "Debug" else "NO"})
        definitions = values.get("GCC_PREPROCESSOR_DEFINITIONS", ["$(inherited)"])
        values["GCC_PREPROCESSOR_DEFINITIONS"] = definitions + (["DEBUG=1"] if name == "Debug" else ["NDEBUG=1"])
        existing = existing_objects.get(uid(f"config:{key}:{name}"), {}).get("buildSettings", {})
        for signing in ("DEVELOPMENT_TEAM", "PRODUCT_BUNDLE_IDENTIFIER", "CODE_SIGN_IDENTITY", "CODE_SIGN_STYLE",
                        "PROVISIONING_PROFILE_SPECIFIER", "PROVISIONING_PROFILE", "CODE_SIGN_ENTITLEMENTS"):
            if signing in existing:
                values[signing] = existing[signing]
        identifiers.append(obj(f"config:{key}:{name}", "XCBuildConfiguration", name=name, buildSettings=values))
    return obj("configs:" + key, "XCConfigurationList", buildConfigurations=identifiers,
               defaultConfigurationIsVisible=0, defaultConfigurationName="Release")


manifest = json.loads((ROOT / "apple/sources.json").read_text())
rml = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "third_party/rmlui/Source/Core").glob("*.cpp"))
rml += sorted(str(p.relative_to(ROOT)) for p in (ROOT / "third_party/rmlui/Source/Core").glob("*/*.cpp"))
rml += ["third_party/rmlui/Backends/RmlUi_Platform_SDL.cpp",
        "third_party/rmlui/Backends/RmlUi_Renderer_SDL.cpp"]

common_settings = {
    "CLANG_CXX_LANGUAGE_STANDARD": "c++20",
    "CLANG_CXX_LIBRARY": "libc++",
    "CLANG_ENABLE_OBJC_ARC": "YES",
    "GCC_C_LANGUAGE_STANDARD": "gnu11",
    "ENABLE_USER_SCRIPT_SANDBOXING": "NO",
    "ENABLE_HARDENED_RUNTIME": "NO",
    "ENABLE_APP_SANDBOX": "NO",
    "ALWAYS_SEARCH_USER_PATHS": "NO",
    "COPY_PHASE_STRIP": "NO",
    "VF2_DEPS_DIR": "$(PROJECT_TEMP_DIR)/Dependencies/$(CONFIGURATION)$(EFFECTIVE_PLATFORM_NAME)/$(ARCHS)",
}

project_configs = configs("project", common_settings)
products = obj("group:products", "PBXGroup", name="Products", children=[], sourceTree="<group>")
frameworks_group = obj("group:frameworks", "PBXGroup", name="Frameworks", children=[], sourceTree="<group>")
frameworks = {}
targets = []
app_targets = {}
for platform in ("ios", "macos"):
    is_ios = platform == "ios"
    name = "VF2 iOS" if is_ios else "VF2 macOS"
    platform_settings = {
        "SDKROOT": "iphoneos" if is_ios else "macosx",
        "SUPPORTED_PLATFORMS": "iphoneos iphonesimulator" if is_ios else "macosx",
    }
    if is_ios:
        platform_settings.update({"IPHONEOS_DEPLOYMENT_TARGET": "16.0",
                                  "TARGETED_DEVICE_FAMILY": "1,2",
                                  "SUPPORTS_MACCATALYST": "NO",
                                  "SUPPORTS_MAC_DESIGNED_FOR_IPHONE_IPAD": "NO"})
    else:
        platform_settings["MACOSX_DEPLOYMENT_TARGET"] = "15.0"

    dependency_phase = obj(f"phase:deps:{platform}", "PBXShellScriptBuildPhase",
        buildActionMask=2147483647, files=[], inputPaths=[], outputPaths=[
            "$(VF2_DEPS_DIR)/_deps/sdl3-build/libSDL3.a",
            "$(VF2_DEPS_DIR)/_deps/freetype-build/libfreetype.a",
            "$(VF2_DEPS_DIR)/version.h", "$(VF2_DEPS_DIR)/LICENSES.txt"],
        runOnlyForDeploymentPostprocessing=0, alwaysOutOfDate=1,
        name="Prepare SDL and FreeType", shellPath="/bin/bash",
        shellScript='exec /bin/bash "$SRCROOT/apple/build-xcode-dependencies.sh"\n')
    dependency_target = obj(f"target:deps:{platform}", "PBXAggregateTarget",
        name=f"{name} Dependencies", productName=f"{name} Dependencies",
        buildConfigurationList=configs("deps:" + platform, {**platform_settings, "SKIP_INSTALL": "YES"}),
        buildPhases=[dependency_phase], dependencies=[])
    targets.append(dependency_target)
    dependency = obj(f"dependency:{platform}", "PBXTargetDependency", target=dependency_target)

    build_files = []
    sources = manifest["common"] + rml + manifest[platform]
    if not is_ios:
        sources += ["third_party/sljit/sljitLir.c"]
    for path in sources:
        settings = {}
        if path.startswith("third_party/rmlui/") or path.endswith("stb_image_impl.cpp"):
            settings["COMPILER_FLAGS"] = "-w"
        if path.endswith("sljitLir.c"):
            settings["COMPILER_FLAGS"] = "-w -O2 -DSLJIT_CONFIG_AUTO=1"
        build_files.append(obj(f"build:{platform}:{path}", "PBXBuildFile", fileRef=file(path),
                               **({"settings": settings} if settings else {})))
    source_phase = obj(f"phase:sources:{platform}", "PBXSourcesBuildPhase",
                       buildActionMask=2147483647, files=build_files, runOnlyForDeploymentPostprocessing=0)

    # Keep the same resource allowlist and directory layout as the CMake apps.
    asset_files = [obj(f"resource:{folder}:{platform}", "PBXBuildFile",
                       fileRef=file(f"assets/{folder}", "folder"))
                   for folder in ("ui", "fonts", "backgrounds")]
    asset_phase = obj(f"phase:assets:{platform}", "PBXCopyFilesBuildPhase",
        buildActionMask=2147483647, files=asset_files, runOnlyForDeploymentPostprocessing=0,
        name="Copy game UI assets", dstPath="assets", dstSubfolderSpec=7)
    resources = []
    license_ref = obj(f"file:licenses:{platform}", "PBXFileReference", name=f"LICENSES ({platform}).txt",
                      path="$(VF2_DEPS_DIR)/LICENSES.txt", sourceTree="<absolute>", lastKnownFileType="text")
    objects[group("apple")]["children"].append(license_ref)
    resources.append(obj(f"resource:licenses:{platform}", "PBXBuildFile", fileRef=license_ref))
    if is_ios:
        resources.append(obj("resource:ios-icon", "PBXBuildFile", fileRef=file("apple/Assets.xcassets", "folder.assetcatalog")))
    else:
        resources.append(obj("resource:icon", "PBXBuildFile", fileRef=file("assets/icon/VF2.icns", "image.icns")))
    resource_phase = obj(f"phase:resources:{platform}", "PBXResourcesBuildPhase",
                         buildActionMask=2147483647, files=resources, runOnlyForDeploymentPostprocessing=0)

    names = ["Foundation", "Metal", "CoreVideo", "QuartzCore", "CoreMedia", "CoreAudio",
             "AudioToolbox", "AVFoundation", "GameController", "CoreHaptics", "UniformTypeIdentifiers"]
    names += (["UIKit", "CoreBluetooth", "CoreGraphics", "CoreMotion", "OpenGLES"] if is_ios else
              ["Cocoa", "IOKit", "ForceFeedback", "Carbon"])
    links = []
    for framework in names:
        if framework not in frameworks:
            frameworks[framework] = obj("framework:" + framework, "PBXFileReference",
                path=f"System/Library/Frameworks/{framework}.framework", name=f"{framework}.framework",
                sourceTree="SDKROOT", lastKnownFileType="wrapper.framework")
            objects[frameworks_group]["children"].append(frameworks[framework])
        values = {"settings": {"ATTRIBUTES": ["Weak"]}} if framework == "CoreHaptics" else {}
        links.append(obj(f"link:{platform}:{framework}", "PBXBuildFile", fileRef=frameworks[framework], **values))
    link_phase = obj(f"phase:frameworks:{platform}", "PBXFrameworksBuildPhase",
                     buildActionMask=2147483647, files=links, runOnlyForDeploymentPostprocessing=0)

    settings = {**platform_settings,
        "PRODUCT_NAME": "VF2 Recomp",
        "PRODUCT_BUNDLE_IDENTIFIER": "org.vf2recomp.apple",
        "MARKETING_VERSION": "0.1.0", "CURRENT_PROJECT_VERSION": "1",
        "CODE_SIGN_STYLE": "Automatic",
        "INFOPLIST_FILE": "apple/xcode/Info-iOS.plist" if is_ios else "apple/xcode/Info-macOS.plist",
        "GENERATE_INFOPLIST_FILE": "NO", "SKIP_INSTALL": "NO",
        "INSTALL_PATH": "$(LOCAL_APPS_DIR)",
        "HEADER_SEARCH_PATHS": ["$(inherited)", "$(SRCROOT)/src/core/shim_include",
            "$(SRCROOT)/third_party/rmlui/Include", "$(SRCROOT)/third_party/rmlui/Backends",
            "$(VF2_DEPS_DIR)", "$(VF2_DEPS_DIR)/_deps/sdl3-src/include",
            "$(VF2_DEPS_DIR)/_deps/sdl3-build/include-revision",
            "$(VF2_DEPS_DIR)/_deps/freetype-src/include", "$(VF2_DEPS_DIR)/_deps/freetype-build/include"],
        "LIBRARY_SEARCH_PATHS": ["$(inherited)", "$(VF2_DEPS_DIR)/_deps/sdl3-build",
                                 "$(VF2_DEPS_DIR)/_deps/freetype-build"],
        "OTHER_LDFLAGS": ["$(inherited)", "-lSDL3", "-lfreetype", "-lz", "-lm", "-lpthread"],
        "GCC_PREPROCESSOR_DEFINITIONS": ["$(inherited)", "RMLUI_STATIC_LIB", "RMLUI_FONT_ENGINE_FREETYPE",
            "RMLUI_SDL_VERSION_MAJOR=3", "VF2_GPU3D_METAL=1", "VF2_NATIVE_CONTROLS=1"] + (["VF2_IOS=1", "VF2_NO_JIT=1"] if is_ios else []),
    }
    if is_ios:
        settings["ASSETCATALOG_COMPILER_APPICON_NAME"] = "AppIcon"
    else:
        settings["CODE_SIGN_IDENTITY"] = "-"
    for search_paths in ("HEADER_SEARCH_PATHS", "LIBRARY_SEARCH_PATHS"):
        settings[search_paths] = [path if path == "$(inherited)" else f'"{path}"'
                                  for path in settings[search_paths]]
    product = obj("product:" + platform, "PBXFileReference", explicitFileType="wrapper.application",
                  path="VF2 Recomp.app", sourceTree="BUILT_PRODUCTS_DIR", includeInIndex=0)
    objects[products]["children"].append(product)
    target = obj("target:" + platform, "PBXNativeTarget", name=name, productName="VF2 Recomp",
        productReference=product, productType="com.apple.product-type.application",
        buildConfigurationList=configs(platform, settings),
        buildPhases=[source_phase, link_phase, asset_phase, resource_phase], buildRules=[], dependencies=[dependency])
    targets.append(target)
    app_targets[name] = target

# Include headers and project maintenance files in the navigator.
for pattern in ("src/**/*.h", "apple/*.h", "apple/*.md", "apple/*.sh", "apple/*.py",
                "apple/*.json", "apple/*.cmake", "apple/*.in", "apple/xcode/*.plist"):
    for path in sorted(ROOT.glob(pattern)):
        file(str(path.relative_to(ROOT)))
file("apple/CMakeLists.txt")
file("README.md")
file("apple/xcode/Info-iOS.plist")
file("apple/xcode/Info-macOS.plist")
objects[groups[""]]["children"] += [frameworks_group, products]
project_id = obj("project", "PBXProject", attributes={"LastUpgradeCheck": "2600",
    "BuildIndependentTargetsInParallel": "YES",
    "TargetAttributes": {target: {"ProvisioningStyle": "Automatic"} for target in app_targets.values()}},
    buildConfigurationList=project_configs, compatibilityVersion="Xcode 14.0", developmentRegion="en",
    hasScannedForEncodings=0, knownRegions=["en", "Base"], mainGroup=groups[""],
    productRefGroup=products, projectDirPath="", projectRoot="", targets=targets)
PROJECT.mkdir(exist_ok=True)
document = {"archiveVersion": 1, "classes": {}, "objectVersion": 56,
            "objects": objects, "rootObject": project_id}
(PROJECT / "project.pbxproj").write_text("// !$*UTF8*$!\n" + serialize(document) + "\n")

replacements = {"MACOSX_BUNDLE_EXECUTABLE_NAME": "$(EXECUTABLE_NAME)",
                "MACOSX_BUNDLE_GUI_IDENTIFIER": "$(PRODUCT_BUNDLE_IDENTIFIER)",
                "MACOSX_BUNDLE_SHORT_VERSION_STRING": "$(MARKETING_VERSION)",
                "MACOSX_BUNDLE_BUNDLE_VERSION": "$(CURRENT_PROJECT_VERSION)",
                "CMAKE_OSX_DEPLOYMENT_TARGET": "$(MACOSX_DEPLOYMENT_TARGET)"}
(ROOT / "apple/xcode").mkdir(exist_ok=True)
for platform in ("iOS", "macOS"):
    content = (ROOT / f"apple/Info-{platform}.plist.in").read_text()
    for old, new in replacements.items():
        content = content.replace("${" + old + "}", new)
    (ROOT / f"apple/xcode/Info-{platform}.plist").write_text(content)

schemes = PROJECT / "xcshareddata/xcschemes"
schemes.mkdir(parents=True, exist_ok=True)
for name, target in app_targets.items():
    scheme = ET.Element("Scheme", LastUpgradeVersion="2600", version="1.3")
    build = ET.SubElement(scheme, "BuildAction", parallelizeBuildables="YES", buildImplicitDependencies="YES")
    entries = ET.SubElement(build, "BuildActionEntries")
    entry = ET.SubElement(entries, "BuildActionEntry", buildForTesting="YES", buildForRunning="YES",
                          buildForProfiling="YES", buildForArchiving="YES", buildForAnalyzing="YES")
    reference = {"BuildableIdentifier": "primary", "BlueprintIdentifier": target,
                 "BuildableName": "VF2 Recomp.app", "BlueprintName": name,
                 "ReferencedContainer": "container:VF2Recomp.xcodeproj"}
    ET.SubElement(entry, "BuildableReference", **reference)
    ET.SubElement(scheme, "TestAction", buildConfiguration="Debug",
                  selectedDebuggerIdentifier="Xcode.DebuggerFoundation.Debugger.LLDB",
                  selectedLauncherIdentifier="Xcode.DebuggerFoundation.Launcher.LLDB",
                  shouldUseLaunchSchemeArgsEnv="YES")
    launch = ET.SubElement(scheme, "LaunchAction", buildConfiguration="Debug",
        selectedDebuggerIdentifier="Xcode.DebuggerFoundation.Debugger.LLDB",
        selectedLauncherIdentifier="Xcode.DebuggerFoundation.Launcher.LLDB", launchStyle="0",
        useCustomWorkingDirectory="NO", ignoresPersistentStateOnLaunch="NO", debugDocumentVersioning="NO",
        debugServiceExtension="internal", allowLocationSimulation="YES")
    runnable = ET.SubElement(launch, "BuildableProductRunnable", runnableDebuggingMode="0")
    ET.SubElement(runnable, "BuildableReference", **reference)
    profile = ET.SubElement(scheme, "ProfileAction", buildConfiguration="Release",
                            shouldUseLaunchSchemeArgsEnv="YES", savedToolIdentifier="",
                            useCustomWorkingDirectory="NO", debugDocumentVersioning="NO")
    runnable = ET.SubElement(profile, "BuildableProductRunnable", runnableDebuggingMode="0")
    ET.SubElement(runnable, "BuildableReference", **reference)
    ET.SubElement(scheme, "AnalyzeAction", buildConfiguration="Debug")
    ET.SubElement(scheme, "ArchiveAction", buildConfiguration="Release", revealArchiveInOrganizer="YES")
    ET.indent(scheme, space="  ")
    scheme_path = schemes / f"{name}.xcscheme"
    if not scheme_path.exists():  # Keep the developer's Run arguments and diagnostics.
        ET.ElementTree(scheme).write(scheme_path, encoding="UTF-8", xml_declaration=True)
print(f"Generated {PROJECT.name}: {', '.join(app_targets)}")
