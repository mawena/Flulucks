# Règles R8 de l'application (lues par le plugin Gradle de Flutter).

# Rappel JNI : fluluks_jni.c cherche onProgress(I)V par son nom (GetMethodID). Aucun code Kotlin ne
# l'appelle, R8 le retirerait sinon et la progression du formatage resterait à 0 % en release.
-keep interface com.mawena.fluluks.engine.FormatProgress { void onProgress(int); }
-keepclassmembers class * implements com.mawena.fluluks.engine.FormatProgress { void onProgress(int); }
