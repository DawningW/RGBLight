Import("env")

env.AddCustomTarget(
    "Build Webpage",
    None,
    "cd $PROJECT_DIR/web && npm install && npm run build"
)

env.AddCustomTarget(
    "Gen OTA Package",
    "$BUILD_DIR/${PROGNAME}.bin",
    "python pack_ota_bin.py $SOURCE"
)
