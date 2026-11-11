plugins {
    id("com.android.application")
}

android {
    namespace = "com.ch592macro"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.ch592macro"
        minSdk = 26
        targetSdk = 35
        versionCode = 4
        versionName = "0.2.2"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    signingConfigs {
        getByName("debug") {
            System.getenv("CH592_DEBUG_KEYSTORE")?.let { storeFile = file(it) }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test:core:1.6.1")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test.uiautomator:uiautomator:2.3.0")
}
