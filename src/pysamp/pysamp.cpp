#include "pysamp.h"
#include "param_converter.h"
#include "pygamemode.h"

PyGamemode* PySAMP::gamemode = nullptr;
TimerManager* PySAMP::timer_manager = nullptr;
CallbacksManager* PySAMP::callbacks = nullptr;


void PySAMP::load()
{
	sampgdk::logprintf("Loading PySAMP...");
	PySAMP::callbacks = new CallbacksManager();
	PySAMP::timer_manager = new TimerManager();
	PySAMP::gamemode = new PyGamemode(PySAMP::callbacks);
	gamemode->load();
}

void PySAMP::reload()
{
	if(!PySAMP::isLoaded())
		return;

	sampgdk::logprintf("Reloading PySAMP...");
	PySAMP::callback("OnGameModeExit");
	PySAMP::disable();
	PySAMP::gamemode->reload();
	PySAMP::timer_manager->enable();
	PySAMP::timer_manager->clear_timers();
	PySAMP::callback("OnGameModeInit");
}

void PySAMP::disable()
{
	if(!PySAMP::isLoaded())
		return;

	PySAMP::gamemode->disable();
	PySAMP::timer_manager->disable();
}

void PySAMP::unload()
{
	if(!PySAMP::isLoaded())
		return;

	sampgdk::logprintf("Unloading PySAMP...");
	PySAMP::disable();
	delete PySAMP::callbacks;
	PySAMP::callbacks = nullptr;
	delete PySAMP::timer_manager;
	PySAMP::timer_manager = nullptr;
	delete PySAMP::gamemode;
	PySAMP::gamemode = nullptr;
}

bool PySAMP::isInitialized()
{
	return !!PySAMP::gamemode;
}

bool PySAMP::isLoaded()
{
	return(
		PySAMP::isInitialized()
		&& PySAMP::gamemode->isLoaded()
	);
}

bool PySAMP::isEnabled()
{
	return(
		PySAMP::isLoaded()
		&& PySAMP::gamemode->isEnabled()
	);
}

void PySAMP::addTimer(Timer& timer)
{
	PySAMP::timer_manager->add_timer(timer);
}

void PySAMP::removeTimer(int id)
{
	PySAMP::timer_manager->remove_timer(id);
}

void PySAMP::processTick(unsigned int currentTick)
{
	if(!PySAMP::isLoaded())
		return;

	PySAMP::callback("OnProcessTick");

	PySAMP::GIL gil;
	PySAMP::timer_manager->process_timers(currentTick);
}

std::string PySAMP::getEncoding()
{
	return std::string(PyBytes_AsString(PyUnicode_AsUTF8String(
		PySAMP::gamemode->getConfig()["encoding"]
	)));
}

int PySAMP::callback(const std::string& name)
{
	return PySAMP::callback(name, NULL);
}

int PySAMP::callback(const std::string& name, PyObject* pArgs)
{
	return PySAMP::callback(name, pArgs, NULL, NULL);
}

int PySAMP::callback(
	const std::string& name,
	PyObject* pArgs,
	cell* retval,
	bool* stop
)
{
	if(!PySAMP::isLoaded())
		return 0;

	return PySAMP::gamemode->callback(
		name,
		pArgs,
		retval,
		stop
	);
}

bool PySAMP::onPlayerCommandText(int playerid, const char* cmdtext)
{
	if(!PySAMP::isLoaded())
		return false;

	PySAMP::GIL gil;
	PyObject* tuple = Py_BuildValue(
		"(iN)",
		playerid,
		PyUnicode_Decode(
			cmdtext,
			strlen(cmdtext),
			PySAMP::getEncoding().c_str(),
			"strict"
		)
	);

	return PySAMP::callback("OnPlayerCommandText", tuple);
}

void PySAMP::registerCallback(const std::string& name, const std::string& format)
{
	if(!PySAMP::isInitialized())
		return;

	PySAMP::callbacks->addFormat(name, format);
}

PyObject* PySAMP::pyConfig(PyObject *self, PyObject *args, PyObject *kwargs)
{
	return PySAMP::gamemode->pyConfig(self, args, kwargs);
}

PyObject* PySAMP::amxParamsToTuple(AMX *amx, const std::string& callback_name, cell *params)
{
	if(!PySAMP::isInitialized())
		return NULL;

	const std::string* format = PySAMP::callbacks->getFormat(callback_name);

	if(format == nullptr)
		return NULL;

	PySAMP::GIL gil;
	PyObject* tuple = ParamConverter::to_tuple(
		params,
		*format,
		amx
	);

	if(tuple == NULL)
	{
		PyObject* name = PyUnicode_FromString(callback_name.c_str());
		PyErr_WriteUnraisable(name);
		Py_DECREF(name);
	}

	return tuple;
}

ParamConverter::ArgumentPool* PySAMP::tupleToAmxParams(PyObject *tuple, bool asReference)
{
	if(!PySAMP::isInitialized())
		return NULL;

	PySAMP::GIL gil;
	ParamConverter::ArgumentPool* params = ParamConverter::from_tuple(tuple);

	return params;
}

PySAMP::GIL::GIL()
{
	gilState = PyGILState_Ensure();
}

PySAMP::GIL::~GIL()
{
	PyGILState_Release(gilState);
}
