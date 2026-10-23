# Règles R8 de l'application (lues par le plugin Gradle de Flutter).

# Rappel JNI : fluluks_jni.c cherche onProgress(I)V par son nom (GetMethodID). Aucun code Kotlin ne
# l'appelle, R8 le retirerait sinon et la progression du formatage resterait à 0 % en release.
-keep interface com.mawena.fluluks.engine.FormatProgress { void onProgress(int); }
-keepclassmembers class * implements com.mawena.fluluks.engine.FormatProgress { void onProgress(int); }

# SFTP (phase 10). JSch choisit ses algorithmes par leur nom de classe (JSch.setConfig puis Class.forName) : toutes
# ses classes sont gardées telles quelles, noms compris. Bouncy Castle n'est appelé que par ces classes, par son API
# légère (Ed25519, X25519, ML-KEM, Argon2…), sans réflexion : R8 suit ces références depuis JSch et retire le reste
# (fournisseur JCA compris, que Portail n'enregistre pas).
-keep class com.jcraft.jsch.** { *; }
# Dépendances facultatives de JSch, absentes d'Android : journaux, agents SSH, Kerberos, sockets Unix.
-dontwarn org.apache.logging.log4j.**
-dontwarn org.slf4j.**
-dontwarn com.sun.jna.**
-dontwarn org.newsclub.net.unix.**
-dontwarn org.ietf.jgss.**
# Parties de Bouncy Castle propres au JDK de bureau (LDAP, JNDI), jamais atteintes sur Android.
-dontwarn javax.naming.**
