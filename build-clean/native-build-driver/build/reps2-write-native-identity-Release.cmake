file(READ "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/build/reps2-native-toolchain-Release.json" toolchain_json)
string(SHA256 cache_key_sha256 "${toolchain_json}")
file(WRITE "D:/REPS2-Project/.worktrees/ee-vblank-timing/build-clean/native-build-driver/build/reps2-native-build-identity-Release.json"
    "{\n  \"schema_version\": 1,\n  \"kind\": \"reps2.native_build_identity\",\n  \"cache_key_sha256\": \"${cache_key_sha256}\",\n  \"toolchain_report\": \"reps2-native-toolchain-Release.json\"\n}\n")
