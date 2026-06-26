Import("env")

def before_build_spiffs(source, target, env):
    env.Execute("cd web && npm run build")
    env.Execute("rm -rf data")
    env.Execute("cp -r web/build data")

env.AddPreAction("$BUILD_DIR/spiffs.bin", before_build_spiffs)
