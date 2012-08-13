/*
 * Copyright (c) 2011, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF 
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "dpoCContext.h"

#include "dpo_debug.h"
#include "dpo_security_checks_stub.h"
#include "dpoCData.h"
#include "dpoCKernel.h"

#include <jsfriendapi.h>
#include <nsIClassInfoImpl.h>
#include <nsStringAPI.h>

/*
 * Implement ClassInfo support to make this class feel more like a JavaScript class, i.e.,
 * it is autmatically casted to the right interface and all methods are available
 * without using QueryInterface.
 * 
 * see https://developer.mozilla.org/en/Using_nsIClassInfo
 */
NS_IMPL_CLASSINFO( dpoCContext, 0, 0, DPO_CONTEXT_CID)
NS_IMPL_CI_INTERFACE_GETTER2(dpoCContext, dpoIContext, nsISecurityCheckedComponent)

/* 
 * Implement the hooks for the cycle collector
 */
NS_IMPL_CYCLE_COLLECTION_CLASS(dpoCContext)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(dpoCContext)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(parent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

//NS_IMPL_CYCLE_COLLECTION_ROOT_BEGIN(dpoCContext)
//NS_IMPL_CYCLE_COLLECTION_ROOT_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(dpoCContext)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(dpoCContext)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(parent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(dpoCContext)
  NS_INTERFACE_MAP_ENTRY(dpoIContext)
  NS_INTERFACE_MAP_ENTRY(nsISecurityCheckedComponent)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, dpoIContext)
  NS_IMPL_QUERY_CLASSINFO(dpoCContext)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(dpoCContext)
NS_IMPL_CYCLE_COLLECTING_RELEASE(dpoCContext)

DPO_SECURITY_CHECKS_ALL( dpoCContext)

#ifdef CLPROFILE
void CL_CALLBACK dpoCContext::CollectTimings( cl_event event, cl_int status, void *data)
{
	cl_int result;
	dpoCContext *instance = (dpoCContext *) data;
	
	DEBUG_LOG_STATUS("CollectTimings", "enquiring for runtimes...");

	result = clGetEventProfilingInfo( event, CL_PROFILING_COMMAND_START, sizeof (cl_ulong), &(instance->clp_exec_start), NULL);
	if (result != CL_SUCCESS) {
		DEBUG_LOG_ERROR("CollectTimings", result);
		instance->clp_exec_start = 0;
	}

	result = clGetEventProfilingInfo( event, CL_PROFILING_COMMAND_END, sizeof (cl_ulong), &(instance->clp_exec_end), NULL);
	if (result != CL_SUCCESS) {
		DEBUG_LOG_ERROR("CollectTimings", result);
		instance->clp_exec_end = 0;
	}

	DEBUG_LOG_STATUS("CollectTimings", "Collected start " << instance->clp_exec_start << " and end " << instance->clp_exec_end);
}
#endif /* CLPROFILE */

void CL_CALLBACK dpoCContext::ReportCLError( const char *err_info, const void *private_info, size_t cb, void *user_data)
{
	DEBUG_LOG_CLERROR(err_info);
}

dpoCContext::dpoCContext(dpoIPlatform *aParent)
{
	DEBUG_LOG_CREATE("dpoCContext", this);
	parent = aParent;
	buildLog = NULL;
	buildLogSize = 0;
	cmdQueue = NULL;
#ifdef CLPROFILE
	clp_exec_start = 0;
	clp_exec_end = 0;
#endif /* CLPROFILE */
#ifdef WINDOWS_ROUNDTRIP
	wrt_exec_start.QuadPart = -1;
	wrt_exec_end.QuadPart = -1;
#endif /* WINDOWS_ROUNDTRIP */
}

nsresult dpoCContext::InitContext(cl_platform_id platform)
{
	cl_int err_code;
	cl_device_id *devices;
	size_t cb;

	cl_context_properties context_properties[3] = {CL_CONTEXT_PLATFORM, (cl_context_properties)platform, NULL};
	
	context = clCreateContextFromType(context_properties, CL_DEVICE_TYPE_CPU, ReportCLError, this, &err_code);
	if (err_code != CL_SUCCESS) {
		DEBUG_LOG_ERROR("InitContext", err_code);
		return NS_ERROR_NOT_AVAILABLE;
	}

	err_code = clGetContextInfo(context, CL_CONTEXT_DEVICES, 0, NULL, &cb);
	if (err_code != CL_SUCCESS) {
		DEBUG_LOG_ERROR("InitContext", err_code);
		return NS_ERROR_NOT_AVAILABLE;
	}

	devices = (cl_device_id *)nsMemory::Alloc(sizeof(cl_device_id)*cb);
	if (devices == NULL) {
		DEBUG_LOG_STATUS("InitContext", "Cannot allocate device list");
		return NS_ERROR_OUT_OF_MEMORY;
	}

	err_code = clGetContextInfo(context, CL_CONTEXT_DEVICES, cb, devices, NULL);
	if (err_code != CL_SUCCESS) {
		DEBUG_LOG_ERROR("InitContext", err_code);
		nsMemory::Free(devices);
		return NS_ERROR_NOT_AVAILABLE;
	}

	cmdQueue = clCreateCommandQueue(context, devices[0], 
#ifdef CLPROFILE 
		CL_QUEUE_PROFILING_ENABLE |
#endif /* CLPROFILE */
#ifdef OUTOFORDERQUEUE
		CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE |
#endif /* OUTOFORDERQUEUE */
		0,
		&err_code);
	if (err_code != CL_SUCCESS) {
		DEBUG_LOG_ERROR("InitContext", err_code);
		nsMemory::Free(devices);
		return NS_ERROR_NOT_AVAILABLE;
	}

	DEBUG_LOG_STATUS("InitContext", "queue is " << cmdQueue);

	nsMemory::Free(devices);

	kernelFailureMem = CreateBuffer(CL_MEM_READ_WRITE, sizeof(int), NULL, &err_code);
	if (err_code != CL_SUCCESS) {
		DEBUG_LOG_ERROR("InitContext", err_code);
		return NS_ERROR_NOT_AVAILABLE;
	}

	return NS_OK;
}

dpoCContext::~dpoCContext()
{
	DEBUG_LOG_DESTROY("dpoCContext", this);
#ifdef INCREMENTAL_MEM_RELEASE
	// free the pending queue
	while (dpoCData::CheckFree()) {};
#endif /* INCREMENTAL_MEM_RELEASE */

	if (buildLog != NULL) {
		nsMemory::Free(buildLog);
	}
	if (cmdQueue != NULL) {
		clReleaseCommandQueue(cmdQueue);
	}
}

/* dpoIKernel compileKernel (in AString source, in AString kernelName, [optional] in AString options); */
NS_IMETHODIMP dpoCContext::CompileKernel(const nsAString & source, const nsAString & kernelName, const nsAString & options, dpoIKernel **_retval NS_OUTPARAM)
{
	cl_program program;
	cl_kernel kernel;
	cl_int err_code, err_code2;
	cl_uint numDevices;
	cl_device_id *devices = NULL;
	size_t actual;
	char *sourceStr, *optionsStr, *kernelNameStr;
	nsCOMPtr<dpoCKernel> ret;
	nsresult result;

	sourceStr = ToNewUTF8String(source);
	DEBUG_LOG_STATUS("CompileKernel", "Source: " << sourceStr);
	program = clCreateProgramWithSource(context, 1, (const char**)&sourceStr, NULL, &err_code);
	nsMemory::Free(sourceStr);
	if (err_code != CL_SUCCESS) {
		DEBUG_LOG_ERROR("CompileKernel", err_code);
		return NS_ERROR_ILLEGAL_VALUE;
	}

	optionsStr = ToNewUTF8String(options);
	err_code = clBuildProgram(program, 0, NULL, optionsStr, NULL, NULL);
	nsMemory::Free(optionsStr);
	if (err_code != CL_SUCCESS) {
		DEBUG_LOG_ERROR("CompileKernel", err_code);
	}
		
	err_code2 = clGetProgramInfo(program, CL_PROGRAM_NUM_DEVICES, sizeof(cl_uint), &numDevices, NULL);
	if (err_code2 != CL_SUCCESS) {
		DEBUG_LOG_ERROR("CompileKernel", err_code2);
		goto FAIL;
	} 

	devices = (cl_device_id *) nsMemory::Alloc(numDevices * sizeof(cl_device_id));
	err_code2 = clGetProgramInfo(program, CL_PROGRAM_DEVICES, numDevices * sizeof(cl_device_id), devices, NULL);
	if (err_code2 != CL_SUCCESS) {
		DEBUG_LOG_ERROR("CompileKernel", err_code);
		goto FAIL;
	} 
	err_code2 = clGetProgramBuildInfo(program, devices[0], CL_PROGRAM_BUILD_LOG, buildLogSize, buildLog, &actual);
	if (actual > buildLogSize) {
		if (buildLog != NULL) {
			nsMemory::Free(buildLog);
		}
		buildLog = (char *) nsMemory::Alloc(actual * sizeof(char));
		if (buildLog == NULL) {
			DEBUG_LOG_STATUS("CompileKernel", "Cannot allocate buildLog");
			buildLogSize = 0;
			goto DONE;
		}
		buildLogSize = actual;
		err_code2 = clGetProgramBuildInfo(program, devices[0], CL_PROGRAM_BUILD_LOG, buildLogSize, buildLog, &actual);
	}
			
	if (err_code2 != CL_SUCCESS) {
		DEBUG_LOG_ERROR("CompileKernel", err_code);
		goto FAIL;
	}

	DEBUG_LOG_STATUS("CompileKernel", "buildLog: " << buildLog);
	goto DONE;

FAIL:
	if (buildLog != NULL) {
		nsMemory::Free(buildLog);
		buildLog = NULL;
		buildLogSize = 0;
	}

DONE:
	if (devices != NULL) {
		nsMemory::Free(devices);
	}
	
	kernelNameStr = ToNewUTF8String(kernelName);
	kernel = clCreateKernel(program, kernelNameStr, &err_code);
	nsMemory::Free( kernelNameStr);
	clReleaseProgram(program);
	if (err_code != CL_SUCCESS) {
		DEBUG_LOG_ERROR("CompileKernel", err_code);
		return NS_ERROR_NOT_AVAILABLE;
	}

	ret = new dpoCKernel(this);
	if (ret == NULL) {
		clReleaseKernel(kernel);
		DEBUG_LOG_STATUS("CompileKernel", "Cannot create new dpoCKernel object");
		return NS_ERROR_OUT_OF_MEMORY;
	}

	/* all kernels share the single buffer for the failure code */
	result = ret->InitKernel(cmdQueue, kernel, kernelFailureMem);
	if (result != NS_OK) {
		clReleaseKernel(kernel);
		return result;
	}

	ret.forget((dpoCKernel **)_retval);
	
	return NS_OK;
}

/* readonly attribute AString buildLog; */
NS_IMETHODIMP dpoCContext::GetBuildLog(nsAString & aBuildLog)
{
	if (buildLog != NULL) {
		aBuildLog.AssignLiteral(buildLog);

		return NS_OK;
	} else {
		return NS_ERROR_NOT_AVAILABLE;
	}
}

nsresult dpoCContext::ExtractArray(const jsval &source, JSObject **result, JSContext *cx)
{
	if (JSVAL_IS_PRIMITIVE( source)) {
		return NS_ERROR_INVALID_ARG;
	}

	*result = JSVAL_TO_OBJECT( source);

	if (!JS_IsTypedArrayObject( *result, cx)) {
		*result = NULL;
		return NS_ERROR_CANNOT_CONVERT_DATA;
	}

	return NS_OK;
}

cl_mem dpoCContext::CreateBuffer(cl_mem_flags flags, size_t size, void *ptr, cl_int *err)
{
#ifdef INCREMENTAL_MEM_RELEASE
	int freed;
	cl_mem result;
	do {
		freed = dpoCData::CheckFree();
		result = clCreateBuffer(context, flags, size, ptr, err);
	} while (((*err == CL_OUT_OF_HOST_MEMORY) || (*err == CL_MEM_OBJECT_ALLOCATION_FAILURE)) && freed);
	return result;
#else INCREMENTAL_MEM_RELEASE
	return clCreateBuffer(context, flags, size, ptr, err);
#endif INCREMENTAL_MEM_RELEASE
}

/* [implicit_jscontext] dpoIData mapData (in jsval source); */
NS_IMETHODIMP dpoCContext::MapData(const jsval & source, JSContext *cx, dpoIData **_retval NS_OUTPARAM)
{
  cl_int err_code;
  nsresult result;
  JSObject *tArray;
  nsCOMPtr<dpoCData> data;

  result = ExtractArray( source, &tArray, cx);
  if (result == NS_OK) {
    // we have a typed array
    data = new dpoCData( this);
    if (data == NULL) {
      DEBUG_LOG_STATUS("MapData", "Cannot create new dpoCData object");
      return NS_ERROR_OUT_OF_MEMORY;
    }

    // USE_HOST_PTR is save as the CData object will keep the associated typed array alive as long as the
    // memory buffer lives
    cl_mem_flags flags = CL_MEM_READ_ONLY;
    void *tArrayBuffer = NULL;
    size_t arrayByteLength = JS_GetTypedArrayByteLength(tArray, cx);
    if(arrayByteLength == 0) {
        arrayByteLength = 1;
    }
    else {
        tArrayBuffer = JS_GetArrayBufferViewData(tArray, cx);
        flags |= CL_MEM_USE_HOST_PTR;
    }

    cl_mem memObj = CreateBuffer(flags, arrayByteLength, tArrayBuffer , &err_code);
    if (err_code != CL_SUCCESS) {
      DEBUG_LOG_ERROR("MapData", err_code);
      return NS_ERROR_NOT_AVAILABLE;
    }

    result = data->InitCData(cx, cmdQueue, memObj, JS_GetTypedArrayType(tArray, cx), JS_GetTypedArrayLength(tArray, cx), 
        JS_GetTypedArrayByteLength(tArray, cx), tArray);

#ifdef SUPPORT_MAPPING_ARRAYS
  } else if (JSVAL_IS_OBJECT(source)) {
    // maybe it is a regular array. 
    //
    // WARNING: We map a pointer to the actual array here. All this works on CPU only
    //          and only of the OpenCL compiler knows what to do! For the current Intel OpenCL SDK
    //          this works but your milage may vary.
    const jsval *elems = UnsafeDenseArrayElements(cx, JSVAL_TO_OBJECT(source));
    if (elems != NULL) {
      data = new dpoCData( this);
      if (data == NULL) {
        DEBUG_LOG_STATUS("MapData", "Cannot create new dpoCData object");
        return NS_ERROR_OUT_OF_MEMORY;
      }
	  cl_mem memObj = CreateBuffer(CL_MEM_COPY_HOST_PTR | CL_MEM_READ_ONLY, sizeof(double *), &elems, &err_code);
      if (err_code != CL_SUCCESS) {
        DEBUG_LOG_ERROR("MapData", err_code);
        return NS_ERROR_NOT_AVAILABLE;
      }
      result = data->InitCData(cx, cmdQueue, memObj, 0 /* bogus type */, 1, sizeof(double *), JSVAL_TO_OBJECT(source));
#ifndef DEBUG_OFF
    } else {
        DEBUG_LOG_STATUS("MapData", "No elements returned!");
#endif /* DEBUG_OFF */
    }
#endif /* SUPPORT_MAPPING_ARRAYS */
  }

  if (result == NS_OK) {
    data.forget((dpoCData **)_retval);
  }

  return result;
}

/* [implicit_jscontext] dpoIData cloneData (in jsval source); */
NS_IMETHODIMP dpoCContext::CloneData(const jsval & source, JSContext *cx, dpoIData **_retval NS_OUTPARAM)
{
	return NS_ERROR_NOT_IMPLEMENTED;
}

/* [implicit_jscontext] dpoIData allocateData (in jsval templ, [optional] in PRUint32 length); */
NS_IMETHODIMP dpoCContext::AllocateData(const jsval & templ, PRUint32 length, JSContext *cx, dpoIData **_retval NS_OUTPARAM)
{
	cl_int err_code;
	nsresult result;
	JSObject *tArray;
	size_t bytePerElements;
	nsCOMPtr<dpoCData> data;

	if (!JS_EnterLocalRootScope(cx)) {
		DEBUG_LOG_STATUS("AllocateData", "Cannot root local scope");
		return NS_ERROR_NOT_AVAILABLE;
	}

	result = ExtractArray( templ, &tArray, cx);
	if (result != NS_OK) {
		return result;
	}

	data = new dpoCData( this);
	if (data == NULL) {
		DEBUG_LOG_STATUS("AllocateData", "Cannot create new dpoCData object");
		return NS_ERROR_OUT_OF_MEMORY;
	}
	
	if (length == 0) {
		DEBUG_LOG_STATUS("AllocateData", "size not provided, assuming template's size");
		length = JS_GetTypedArrayLength(tArray, cx);
	}

	bytePerElements = JS_GetTypedArrayByteLength(tArray, cx) / JS_GetTypedArrayLength(tArray, cx);

	DEBUG_LOG_STATUS("AllocateData", "length " << length << " bytePerElements " << bytePerElements);

#ifdef PREALLOCATE_IN_JS_HEAP
	JSObject *jsArray = js_CreateTypedArray(cx, JS_GetTypedArrayType(tArray), length);
	if (!jsArray) {
		DEBUG_LOG_STATUS("AllocateData", "Cannot create typed array");
		return NS_ERROR_OUT_OF_MEMORY;
	}

	cl_mem memObj = CreateBuffer( CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, 
                                  JS_GetTypedArrayByteLength(jsArray), JS_GetTypedArrayData(jsArray), &err_code);
#else /* PREALLOCATE_IN_JS_HEAP */
	JSObject *jsArray = nsnull;
	cl_mem memObj = CreateBuffer(CL_MEM_READ_WRITE, length * bytePerElements, NULL, &err_code);
#endif /* PREALLOCATE_IN_JS_HEAP */
	if (err_code != CL_SUCCESS) {
		DEBUG_LOG_ERROR("AllocateData", err_code);
		return NS_ERROR_NOT_AVAILABLE;
	}

	result = data->InitCData(cx, cmdQueue, memObj, JS_GetTypedArrayType(tArray, cx), length, length * bytePerElements, jsArray);

	if (result == NS_OK) {
		data.forget((dpoCData **) _retval);
	}

	JS_LeaveLocalRootScope(cx);

    return result;
}

/* [implicit_jscontext] dpoIData allocateData2 (in dpoIData templ, [optional] in PRUint32 length); */
NS_IMETHODIMP dpoCContext::AllocateData2(dpoIData *templ, PRUint32 length, JSContext *cx, dpoIData **_retval NS_OUTPARAM) 
{
	// this cast is only safe as long as no other implementations of the dpoIData interface exist
	dpoCData *cData = (dpoCData *) templ;
	cl_int err_code;
	nsresult result;
	size_t bytePerElements;
	nsCOMPtr<dpoCData> data;
#ifdef PREALLOCATE_IN_JS_HEAP
	jsval jsBuffer;
#endif /* PREALLOCATE_IN_JS_HEAP */

	if (!JS_EnterLocalRootScope(cx)) {
		DEBUG_LOG_STATUS("AllocateData2", "Cannot root local scope");
		return NS_ERROR_NOT_AVAILABLE;
	}

	data = new dpoCData( this);
	if (data == NULL) {
		DEBUG_LOG_STATUS("AllocateData2", "Cannot create new dpoCData object");
		return NS_ERROR_OUT_OF_MEMORY;
	}
	
	if (length == 0) {
		DEBUG_LOG_STATUS("AllocateData2", "length not provided, assuming template's size");
		length = cData->GetLength();
	}

	bytePerElements = cData->GetSize() / cData->GetLength();

	DEBUG_LOG_STATUS("AllocateData2", "length " << length << " bytePerElements " << bytePerElements);

#ifdef PREALLOCATE_IN_JS_HEAP
	JSObject *jsArray = js_CreateTypedArray(cx, cData->GetType(), length);
	if (!jsArray) {
		DEBUG_LOG_STATUS("AllocateData2", "Cannot create typed array");
		return NS_ERROR_OUT_OF_MEMORY;
	}

	cl_mem memObj = CreateBuffer(CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, 
                                 JS_GetTypedArrayByteLength(jsArray), JS_GetTypedArrayData(jsArray), &err_code);
#else /* PREALLOCATE_IN_JS_HEAP */
	JSObject *jsArray = NULL;
	cl_mem memObj = CreateBuffer(CL_MEM_READ_WRITE, length * bytePerElements, NULL, &err_code);
#endif /* PREALLOCATE_IN_JS_HEAP */
	if (err_code != CL_SUCCESS) {
		DEBUG_LOG_ERROR("AllocateData2", err_code);
		return NS_ERROR_NOT_AVAILABLE;
	}

	result = data->InitCData(cx, cmdQueue, memObj, cData->GetType(), length, length * bytePerElements, jsArray);

	if (result == NS_OK) {
		data.forget((dpoCData **) _retval);
	}

	JS_LeaveLocalRootScope(cx);
		
    return result;
}
	
/* [implicit_jscontext] bool canBeMapped (in jsval source); */
NS_IMETHODIMP dpoCContext::CanBeMapped(const jsval & source, JSContext* cx, bool *_retval NS_OUTPARAM)
{
#ifdef SUPPORT_MAPPING_ARRAYS
  if (!JSVAL_IS_OBJECT(source)) {
    *_retval = false;
  } else {
    *_retval = IsNestedDenseArrayOfFloats(cx, JSVAL_TO_OBJECT(source));
  }
#else /* SUPPORT_MAPPING_ARRAYS */
  *_retval = false;
#endif /* SUPPORT_MAPPING_ARRAYS */

  return NS_OK;
}

/* readonly attribute PRUint64 lastExecutionTime; */
NS_IMETHODIMP dpoCContext::GetLastExecutionTime(PRUint64 *_retval NS_OUTPARAM) 
{
#ifdef CLPROFILE
	if ((clp_exec_end == 0) || (clp_exec_start == 0)) {
		*_retval = 0;
		return NS_ERROR_NOT_AVAILABLE;
	} else {
		*_retval = clp_exec_end - clp_exec_start;
		return NS_OK;
	}
#else /* CLPROFILE */
	return NS_ERROR_NOT_IMPLEMENTED;
#endif /* CLPROFILE */
}

/* readonly attribute PRUint64 lastRoundTripTime; */
NS_IMETHODIMP dpoCContext::GetLastRoundTripTime(PRUint64 *_retval NS_OUTPARAM) 
{
#ifdef WINDOWS_ROUNDTRIP
	if ((wrt_exec_start.QuadPart == -1) || (wrt_exec_end.QuadPart == -1)) {
		*_retval = 0;
		return NS_ERROR_NOT_AVAILABLE;
	} else {
		LARGE_INTEGER freq;
		if (!QueryPerformanceFrequency(&freq)) {
			DEBUG_LOG_STATUS("GetLastRoundTrupTime", "cannot read performance counter frequency.");
			return NS_ERROR_NOT_AVAILABLE;
		}
		double diff = (double) (wrt_exec_end.QuadPart - wrt_exec_start.QuadPart);
		double time = diff / (double) freq.QuadPart * 1000000000;
		*_retval = (PRUint64) time;
		return NS_OK;
	}
#else /* WINDOWS_ROUNDTRIP */
	return NS_ERROR_NOT_IMPLEMENTED;
#endif /* WINDOWS_ROUNDTRIP */
}

#ifdef WINDOWS_ROUNDTRIP
void dpoCContext::RecordBeginOfRoundTrip(dpoIContext *parent) {
	dpoCContext *self = (dpoCContext *) parent;
	if (!QueryPerformanceCounter(&(self->wrt_exec_start))) {
		DEBUG_LOG_STATUS("RecordBeginOfRoundTrip", "querying performance counter failed");
		self->wrt_exec_start.QuadPart = -1;
	}
}

void dpoCContext::RecordEndOfRoundTrip(dpoIContext *parent) {
	dpoCContext *self = (dpoCContext *) parent;
	if (self->wrt_exec_start.QuadPart == -1) {
		DEBUG_LOG_STATUS("RecordEndOfRoundTrip", "no previous start data");
		return;
	}
	if (!QueryPerformanceCounter(&(self->wrt_exec_end))) {
		DEBUG_LOG_STATUS("RecordEndOfRoundTrip", "querying performance counter failed");
		self->wrt_exec_start.QuadPart = -1;
		self->wrt_exec_end.QuadPart = -1;
	}
}
#endif /* WINDOWS_ROUNDTRIP */
