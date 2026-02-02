// Top-level build file where you can add configuration options common to all sub-projects/modules.

buildscript {
    repositories {
        google()
        mavenCentral()
    }
    dependencies {
        classpath("com.android.tools.build:gradle:8.8.1")
        classpath("org.jetbrains.kotlin:kotlin-gradle-plugin:1.9.20")
    }
}

allprojects {
    ext {
        set("compose_compiler_version", "1.5.4")
        set("compose_bom_version", "2023.08.00")
    }
}

tasks.register("clean", Delete::class) {
    delete(rootProject.buildDir)
}
