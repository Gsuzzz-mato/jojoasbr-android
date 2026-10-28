package io.github.jojorecomp;

/** The import on the phone (games/jojoasbr/import/jojo_import_jni.cpp): only in ImportActivity's process. */
final class Importer {
    static {
        System.loadLibrary("jojoasbrimport");
    }

    /** Blocking: 0 when the shaders are ready in dataDir/spirv, else see error(). */
    static native int run(int baseFd, String baseName, int updateFd, String updateName, String keysPath, String titleKeysPath, String dataDir, String workDir,
                          int jobs);

    /** {phase, done, total}: 1 opening the game, 2 extracting the shaders, 3 translating them, 4 finishing, 5 done. */
    static native int[] progress();

    static native String error();

    private Importer() {}
}
