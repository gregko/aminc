aminc
=====

Version Code Incrementer for compiled AndroidManifest.xml files

This little program is needed to create separate APK files for each processor architecture. The procedure would be as follows:

1. Build your APK with any tools you use, containing all native code libraries you support, e.g. armeabi, armeabi-v7a, x86 and mips. I'll call it the 'original' APK file.

2. Unzip your original APK to an empty folder, with any zip/unzip utility, best use command line tools, so that you could automate it with a shell script or batch file.

3. In the folder where original APK was uncompressed to, delete META-INF sub-folder (this contains the signatures, we'll need to re-sign it after all the modifications, so the original META-INF must be deleted).

4. Change to lib sub-folder, and delete the sub-folders for any other platforms you don't want in the new APK file. For example, leave only 'x86' sub-folder to make an APK for Intel Atom processors.

5. Important: each APK for a different architecture, must have a different versionCode number in AndroidManifest.xml, and the version code for e.g. armeabi-v7a must be slightly higher than the one for armeabi (read Google directions for creating multiple APKs here: http://developer.android.com/google/play/publishing/multiple-apks.html ). Unfortunately, the manifest file is in a compiled binary form inside the APK. We need a special tool for modifying the versionCode there. See below.

6. Once the manifest is modified with new version code, and unnecessary directories and files deleted, re-zip, sign and align your smaller APK (use jarsigner and zipalign tools from Android SDK).

The only outstanding issue is the way to modify ‘versionCode’ in binary manifest file. I could not find a solution for this for a long time, so finally had to sit down and crank my own code to do this. As the starting point, I took APKExtractor by Prasanta Paul, http://prasanta-paul.blogspot.com, written in Java, but I’m the old school and still more comfortable with C++, so this GitHub repository contains my code to do this.
