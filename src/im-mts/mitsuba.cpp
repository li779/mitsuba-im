/*
    This file is part of Mitsuba, a physically based rendering system.

    Copyright (c) 2007-2014 by Wenzel Jakob and others.

    Mitsuba is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License Version 3
    as published by the Free Software Foundation.

    Mitsuba is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <mitsuba/core/platform.h>
#ifdef HAS_EIGEN
#include <mitsuba/core/shvector.h>
#endif
#include <mitsuba/core/sched.h>
#include <mitsuba/core/plugin.h>
#include <mitsuba/core/fresolver.h>
#include <mitsuba/core/fstream.h>
#include <mitsuba/core/appender.h>
#include <mitsuba/core/bitmap.h>
#include <mitsuba/core/statistics.h>
#include <mitsuba/render/sceneloader.h>

#if defined(__OSX__)
#include <ApplicationServices/ApplicationServices.h>
#endif

#if !defined(__WINDOWS__)
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#endif

using namespace mitsuba;

/// ================================================================
///  Handle application crashes when compiled with MTS_HAS_BREAKPAD
/// ================================================================
#if defined(MTS_HAS_BREAKPAD)
#if defined(__OSX__)

extern void *__mts_init_breakpad_osx();
extern void  __mts_destroy_breakpad_osx(void *);

#elif defined(__WINDOWS__)

#include <breakpad/client/windows/sender/crash_report_sender.h>
#include <breakpad/client/windows/handler/exception_handler.h>

static bool minidumpCallbackWindows(const wchar_t *dump_path,
		const wchar_t *minidump_id, void *context,
		EXCEPTION_POINTERS *exinfo, MDRawAssertionInfo *assertion,
		bool succeeded) {
	if (!dump_path || !minidump_id)
		return false;

	std::wstring filename = std::wstring(dump_path) + std::wstring(L"\\")
		+ std::wstring(minidump_id) + std::wstring(L".dmp");

	google_breakpad::CrashReportSender sender(L"");
	std::map<std::wstring, std::wstring> parameters;

	#if defined(WIN64)
		parameters[L"prod"] = L"Mitsuba/Win64";
	#else
		parameters[L"prod"] = L"Mitsuba/Win32";
	#endif

	std::string version = MTS_VERSION;
	std::wstring wVersion;
	wVersion.assign(version.begin(), version.end());
	parameters[L"ver"] = wVersion;

	std::wstring resultString;

	if (MessageBox(NULL, TEXT("Mitsuba crashed due to an internal error. If you agree below, a brief "
			"report describing the failure will be submitted to the developers. If you would like to "
			"accelerate the debugging process further, please also create a ticket with information on "
			"the steps that led to the problem on https://www.mitsuba-renderer.org/tracker -- thank you!"),
			TEXT("Error"), MB_OKCANCEL | MB_ICONERROR) != IDOK)
		return false;

	google_breakpad::ReportResult result =
		sender.SendCrashReport(L"http://www.mitsuba-renderer.org/bugreport.php",
		parameters, filename, &resultString);

	if (result != google_breakpad::RESULT_SUCCEEDED) {
		MessageBox(NULL, TEXT("The error report could not be submitted due to a lack of "
			"internet connectivity!"), TEXT("Error"), MB_OK | MB_ICONERROR);
		return false;
	}

	return succeeded;
}

#endif
#endif

/// ================================================================

#if defined(__OSX__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

int mitsuba_start(int argc, char *argv[]) {
	int retval = -1;

	/* Initialize the core framework */
	Class::staticInitialization();
	Object::staticInitialization();
	PluginManager::staticInitialization();
	Statistics::staticInitialization();
	Thread::staticInitialization();
	Logger::staticInitialization();
	FileStream::staticInitialization();
	Thread::initializeOpenMP(getCoreCount());
	Spectrum::staticInitialization();
	Bitmap::staticInitialization();
	Scheduler::staticInitialization();
#ifdef HAS_EIGEN
	SHVector::staticInitialization();
#endif
	SceneLoader::staticInitialization();

	try {
		/* Disable the default appenders */
		ref<Logger> logger = Thread::getThread()->getLogger();
#if 0
		for (size_t i=0; i<logger->getAppenderCount(); ++i) {
			Appender *appender = logger->getAppender(i);
			if (appender->getClass()->derivesFrom(MTS_CLASS(StreamAppender)))
				logger->removeAppender(appender);
		}
#endif

		std::string hostName = "offline";
		try {
			hostName = getHostName();
		} catch (...) { }

#if defined(__OSX__)
		/* Create a log file inside the application bundle */
		MTS_AUTORELEASE_BEGIN()
		logger->addAppender(new StreamAppender(formatString("%s/mitsuba.%s.log",
			__mts_bundlepath().c_str(), hostName.c_str())));
		MTS_AUTORELEASE_END()

		/* Set application defaults (disable OSX synchronization feature) */
		__mts_set_appdefaults();
#else
		/* Create a log file inside the current working directory */
		logger->addAppender(new StreamAppender(formatString("mitsuba.%s.log", hostName.c_str())));
#endif

		/* Correct number parsing on some locales (e.g. ru_RU) */
		setlocale(LC_NUMERIC, "C");

#if defined(MTS_HAS_BREAKPAD)
	#if defined(__OSX__)
		void *breakpad = __mts_init_breakpad_osx();
	#elif defined(__WINDOWS__)
		_CrtSetReportMode(_CRT_ASSERT, 0);
		std::wstring dump_path;
		dump_path.resize(1024);
		GetTempPathW(1024, &dump_path[0]);

		google_breakpad::ExceptionHandler *breakpad =
			new google_breakpad::ExceptionHandler(
				dump_path, NULL, minidumpCallbackWindows, NULL,
				google_breakpad::ExceptionHandler::HANDLER_ALL,
				MiniDumpNormal, NULL, NULL);
	#endif
#endif

		retval = 0;
	} catch (const std::exception &e) {
		SLog(EWarn, "Critical exception during startup: %s", e.what());
	}

	try {
		int nprocs_avail = getCoreCount(), nprocs = nprocs_avail;
		/* Initialize OpenMP */
		Thread::initializeOpenMP(nprocs);
	} catch (const std::exception &e) {
		SLog(EWarn, "Critical exception during OpenMP startup: %s", e.what());
	}

	try {
		int nprocs_avail = getCoreCount(), nprocs = nprocs_avail;
		/* Configure the scheduling subsystem */
		Scheduler *scheduler = Scheduler::getInstance();
		bool useCoreAffinity = nprocs == nprocs_avail;
		for (int i=0; i<nprocs; ++i)
			scheduler->registerWorker(new LocalWorker(useCoreAffinity ? i : -1,
				formatString("wrk%i", i)));
		scheduler->start();
	} catch (const std::exception &e) {
		SLog(EWarn, "Critical exception during scheduler startup: %s", e.what());
	}

	return retval;
}

void mitsuba_shutdown() {
	try {
		Scheduler *scheduler = Scheduler::getInstance();
		scheduler->stop();
	}
	catch (const std::exception &e) {
		SLog(EWarn, "Critical exception during scheduler shutdown: %s", e.what());
	}

	try {
		Statistics::getInstance()->printStats();

#if defined(MTS_HAS_BREAKPAD)
	#if defined(__OSX__)
		__mts_destroy_breakpad_osx(breakpad);
	#elif defined(__WINDOWS__)
		delete breakpad;
	#endif
#endif
	} catch (const std::exception &e) {
		SLog(EWarn, "Critical exception during shutdown: %s", e.what());
	}

	/* Shutdown the core framework */
	SceneLoader::staticShutdown();
#ifdef HAS_EIGEN
	SHVector::staticInitialization();
#endif
	Scheduler::staticShutdown();
	Bitmap::staticShutdown();
	Spectrum::staticShutdown();
	FileStream::staticShutdown();
	Logger::staticShutdown();
	Thread::staticShutdown();
	Statistics::staticShutdown();
	PluginManager::staticShutdown();
	Object::staticShutdown();
	Class::staticShutdown();
}
