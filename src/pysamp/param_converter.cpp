#include <functional>
#include <unordered_map>

extern "C"
{
	#include "internal/fakeamx.h"
}

#include "param_converter.h"
#include "pysamp.h"


static PyObject* arg_to_py(AMX* amx, char type, cell value);


ParamConverter::ArgumentPool::ArgumentPool(int number_of_args)
{
	this->number_of_args = number_of_args;
	this->number_of_args_by_ref = 0;
	this->args_by_ref = new Argument[number_of_args];

	this->amx_args = new cell[number_of_args + 1];
	this->amx_args[0] = number_of_args * sizeof(cell);
}


ParamConverter::ArgumentPool::~ArgumentPool()
{
	delete[] args_by_ref;
	delete[] amx_args;
}


void ParamConverter::ArgumentPool::UpdateArgsByRef()
{
	ParamConverter::Argument* arg;
	cell value;
	PyObject* new_object;
	int old_refcnt;
	char type;
	PyObject* item;

	for (int index = 0; index < this->number_of_args_by_ref; index++)
	{
		arg = &this->args_by_ref[index];
		sampgdk_fakeamx_get_cell((int)*arg->addr, &value);

		item = PyList_GetItem(arg->object, 0);
		if (PyLong_Check(item))
		{
			type = 'i';
		}
		else if (PyFloat_Check(item))
		{
			type = 'f';
		}
		else if (PyBool_Check(item))
		{
			type = 'b';
		}
		else if (PyUnicode_Check(item))
		{
			type = 's';
		}
		else
		{
			continue;
		}

		new_object = arg_to_py(sampgdk_fakeamx_amx(), type, value);

		old_refcnt = Py_REFCNT(arg->object);
		PyList_SetItem(arg->object, 0, new_object);
	}
}


std::unordered_map<char, std::function<bool (PyObject*)>> format_map = {
	{'b', [](PyObject *object) -> bool { return PyBool_Check(object); }},
	{'d', [](PyObject *object) -> bool { return PyLong_Check(object); }},
	{'f', [](PyObject *object) -> bool { return PyFloat_Check(object); }},
	{'s', [](PyObject *object) -> bool { return PyUnicode_Check(object); }},
};


std::string ParamConverter::get_format(PyObject *tuple)
{
	std::string format;

	for(Py_ssize_t i = 0; i < PyTuple_Size(tuple); ++i)
	{
		PyObject *current_argument = PyTuple_GetItem(tuple, i);

		for(const auto& item : format_map)
		{
			if(item.second(current_argument))
			{
				format += item.first;
				break;
			}
		}
	}

	return format;
}


Py_ssize_t count_args(PyObject* tuple)
{
	Py_ssize_t count = 0;

	for(Py_ssize_t i = 0; i < PyTuple_Size(tuple); ++i)
	{
		PyObject *item = PyTuple_GetItem(tuple, i);

		if(!PyTuple_Check(item))
		{
			++count;
			continue;
		}

		count += PyTuple_Size(item);
	}

	return count;
}


void ParamConverter::amx_pop_params(cell *amx_params, PyObject *tuple)
{
	for(Py_ssize_t index = 0; index < PyTuple_Size(tuple); ++index)
	{
		PyObject *current_argument = PyTuple_GetItem(tuple, index);

		if(PyUnicode_Check(current_argument))
		{
			sampgdk_fakeamx_pop(amx_params[index + 1]);
		}
		else if(PyTuple_Check(current_argument))
		{
			Py_ssize_t tuple_size = PyTuple_Size(current_argument);

			for(
				Py_ssize_t tuple_index = 0;
				tuple_index < tuple_size;
				++tuple_index
				) {
				sampgdk_fakeamx_pop(amx_params[index + tuple_index + 1]);
			}
			index += tuple_size - 1;
		}
	}
}


void append_by_reference(PyObject *tuple, cell *amx_params, Py_ssize_t start_index)
{
	Py_ssize_t max_index = PyList_Size(tuple) + start_index;

	for(Py_ssize_t amx_index = start_index; amx_index < max_index; ++amx_index)
	{
		PyObject *current_argument = PyList_GetItem(tuple, amx_index - start_index);

		if(PyBool_Check(current_argument))
		{
			bool value = PyObject_IsTrue(current_argument);
			sampgdk_fakeamx_push_cell(value, &amx_params[amx_index]);
		}
		else if(PyLong_Check(current_argument))
		{
			unsigned int value = PyLong_AsUnsignedLongMask(current_argument);
			sampgdk_fakeamx_push_cell(value, &amx_params[amx_index]);
		}
		else if(PyFloat_Check(current_argument))
		{
			float value = (float)PyFloat_AsDouble(current_argument);
			sampgdk_fakeamx_push_float(value, &amx_params[amx_index]);
		}
		else if(PyUnicode_Check(current_argument))
		{
			const char* value = PyBytes_AsString(
				PyUnicode_AsUTF8String(current_argument)
			);
			sampgdk_fakeamx_push_string(value, NULL, &amx_params[amx_index]);
		}
	}
}


static PyObject* arg_to_py(AMX* amx, char type, cell value)
{
	PyObject* argument;
	switch (type)
	{
	case 'i':
	case 'd':
		argument = PyLong_FromLong((int)value);
		break;
	case 'b':
		argument = value ? Py_True : Py_False;
		break;
	case 'f':
		argument = PyFloat_FromDouble((double)amx_ctof(value));
		break;
	case 's':
	{
		int length = -1;
		char* string_value = NULL;
		cell* phys_addr = NULL;


		if (amx_GetAddr(amx, value, &phys_addr) != AMX_ERR_NONE)
		{
			argument = Py_None;
			break;
		}

		amx_StrLen(phys_addr, &length);

		if (length == -1)
		{
			argument = Py_None;
			break;
		}

		string_value = (char*)malloc((length + 1) * sizeof(char));

		if (
			amx_GetString(
				string_value,
				phys_addr,
				0,
				length + 1
			) != AMX_ERR_NONE
			|| string_value == NULL
			)
		{
			free(string_value);
			argument = Py_None;
			break;
		}

		argument = PyUnicode_Decode(
			string_value,
			length,
			PySAMP::getEncoding().c_str(),
			"strict"
		);
		free(string_value);
		break;
	}
	default:
	{
		argument = nullptr;
	}
	}
	return argument;
}


static bool put_arg(ParamConverter::ArgumentPool* pool, Py_ssize_t index, PyObject* arg)
{
	if (PyBool_Check(arg))
	{
		bool value = PyObject_IsTrue(arg);
		pool->amx_args[index + 1] = value;
	}
	else if (PyLong_Check(arg))
	{
		unsigned int value = PyLong_AsUnsignedLongMask(arg);
		pool->amx_args[index + 1] = value;
	}
	else if (PyFloat_Check(arg))
	{
		float value = (float)PyFloat_AsDouble(arg);
		pool->amx_args[index + 1] = amx_ftoc(value);
	}
	else if (PyUnicode_Check(arg))
	{
		const char* value = PyBytes_AsString(
			PyUnicode_AsUTF8String(arg)
		);
		sampgdk_fakeamx_push_string(value, NULL, &pool->amx_args[index + 1]);
	}
	else if (PyList_Check(arg) && PyList_Size(arg) == 1)
	{
		append_by_reference(arg, pool->amx_args, index + 1);

		int number_of_args_by_ref;
		number_of_args_by_ref = pool->number_of_args_by_ref;

		pool->args_by_ref[number_of_args_by_ref].addr = &pool->amx_args[index + 1];
		pool->args_by_ref[number_of_args_by_ref].object = arg;
		pool->number_of_args_by_ref++;
	}
	else
	{
		return false;
	}
	return true;
}


ParamConverter::ArgumentPool* ParamConverter::from_tuple(PyObject *tuple)
{
	Py_ssize_t len_tuple = count_args(tuple);

	ArgumentPool *args = new ArgumentPool(len_tuple);

	PyObject *current_argument = NULL;

	for (Py_ssize_t index = 0; index < len_tuple; index++)
	{
		current_argument = PyTuple_GetItem(tuple, index);
		if (!put_arg(args, index, current_argument))
		{
			PyErr_Format(
				PyExc_TypeError,
				"Could not convert argument %R in position %d",
				current_argument,
				index + 1
			);
			ParamConverter::amx_pop_params(args->amx_args, tuple);
			return NULL;
		}

		if (PyTuple_Check(current_argument))
		{
			index += PyTuple_Size(current_argument) - 1;
		}
	}

	return args;
}

PyObject* ParamConverter::to_tuple(cell* params, const std::string format, AMX* amx)
{
	int number_of_arguments = format.length();

	if(number_of_arguments != params[0] / sizeof(cell))
	{
		PyErr_Format(
			PyExc_ValueError,
			"Invalid argument count for callback: expected %d, got %d",
			number_of_arguments,
			params[0] / sizeof(cell)
		);
		return NULL;
	}

	PyObject *arguments = PyTuple_New(number_of_arguments);

	for (int i = 0; i < number_of_arguments; ++i)
	{
		const char type = format.at(i);
		cell param = params[i + 1];
		PyObject* argument = arg_to_py(amx, type, param);

		PyTuple_SetItem(arguments, i, argument);
	}

	return arguments;
}
