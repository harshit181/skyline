// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <csignal>
#include <unistd.h>
#include "skyline/common.h"
#include "skyline/os.h"
#include "skyline/jvm.h"
#include "skyline/input.h"

bool Halt;
jobject Surface;
uint FaultCount;
skyline::GroupMutex JniMtx;
skyline::u16 fps;
skyline::u32 frametime;
skyline::Input *input;

void signalHandler(int signal) {
    syslog(LOG_ERR, "Halting program due to signal: %s", strsignal(signal));
    if (FaultCount > 2)
        exit(SIGKILL);
    else
        Halt = true;
    FaultCount++;
}

extern "C" JNIEXPORT void Java_emu_skyline_EmulationActivity_executeApplication(JNIEnv *env, jobject instance, jstring romUriJstring, jint romType, jint romFd, jint preferenceFd, jint logFd) {
    Halt = false;
    FaultCount = 0;
    fps = 0;
    frametime = 0;

    std::signal(SIGTERM, signalHandler);
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGINT, signalHandler);
    std::signal(SIGILL, signalHandler);
    std::signal(SIGABRT, signalHandler);
    std::signal(SIGFPE, signalHandler);

    setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), -8); // Set the priority of this process to the highest value

    auto jvmManager = std::make_shared<skyline::JvmManager>(env, instance);
    auto settings = std::make_shared<skyline::Settings>(preferenceFd);
    auto logger = std::make_shared<skyline::Logger>(logFd, static_cast<skyline::Logger::LogLevel>(std::stoi(settings->GetString("log_level"))));
    //settings->List(logger); // (Uncomment when you want to print out all settings strings)

    auto start = std::chrono::steady_clock::now();

    try {
        auto romUri = env->GetStringUTFChars(romUriJstring, nullptr);
        logger->Info("Launching ROM {}", romUri);
        env->ReleaseStringUTFChars(romUriJstring, romUri);

        skyline::OS os(jvmManager, logger, settings);
        os.Execute(romFd, static_cast<skyline::TitleFormat>(romType));
    } catch (std::exception &e) {
        logger->Error(e.what());
    } catch (...) {
        logger->Error("An unknown exception has occurred");
    }

    logger->Info("Emulation has ended");

    auto end = std::chrono::steady_clock::now();
    logger->Info("Done in: {} ms", (std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()));
}

extern "C" JNIEXPORT void Java_emu_skyline_EmulationActivity_setHalt(JNIEnv *, jobject, jboolean halt) {
    JniMtx.lock(skyline::GroupMutex::Group::Group2);
    Halt = halt;
    JniMtx.unlock();
}

extern "C" JNIEXPORT void Java_emu_skyline_EmulationActivity_setSurface(JNIEnv *env, jobject, jobject surface) {
    JniMtx.lock(skyline::GroupMutex::Group::Group2);
    if (!env->IsSameObject(Surface, nullptr))
        env->DeleteGlobalRef(Surface);
    if (!env->IsSameObject(surface, nullptr))
        Surface = env->NewGlobalRef(surface);
    else
        Surface = surface;
    JniMtx.unlock();
}

extern "C" JNIEXPORT jint Java_emu_skyline_EmulationActivity_getFps(JNIEnv *, jobject) {
    return fps;
}

extern "C" JNIEXPORT jfloat Java_emu_skyline_EmulationActivity_getFrametime(JNIEnv *, jobject) {
    return static_cast<float>(frametime) / 100;
}

extern "C" JNIEXPORT void JNICALL Java_emu_skyline_EmulationActivity_setButtonState(JNIEnv *, jobject, jlong id, jint state) {
    if (input) {
        skyline::input::NpadButton button{.raw = static_cast<skyline::u64>(id)};
        input->npad.at(skyline::input::NpadId::Player1).SetButtonState(button, static_cast<skyline::input::NpadButtonState>(state));
    }
}

extern "C" JNIEXPORT void JNICALL Java_emu_skyline_EmulationActivity_setAxisValue(JNIEnv *, jobject, jint id, jint value) {
    if (input)
        input->npad.at(skyline::input::NpadId::Player1).SetAxisValue(static_cast<skyline::input::NpadAxisId>(id), value);
}
