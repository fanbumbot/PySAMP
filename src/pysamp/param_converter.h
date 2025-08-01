#ifndef PARAMCONVERTER_H
#define PARAMCONVERTER_H

#include <string>

#include "sampgdk.h"
#include "amx/amx.h"

#include "limited_api_python.h"  // IWYU pragma: keep


namespace ParamConverter
{
	struct Argument {
		cell* addr;
		PyObject* object;
	};

	struct ArgumentPool {
		int number_of_args;
		int number_of_args_by_ref;
		Argument* args_by_ref;
		cell* amx_args;

		ArgumentPool(int number_of_args);
		~ArgumentPool();

		void UpdateArgsByRef();
	};

	std::string get_format(PyObject *tuple);
	void amx_pop_params(cell *params, PyObject *tuple);
	ParamConverter::ArgumentPool* from_tuple(PyObject *tuple);
	PyObject* to_tuple(cell *params, const std::string format, AMX *amx);
};

#endif
