/*
Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "kernels.h"

struct ScaleLayerLocalData {
    NeuralNetworkCommonHandle * handle;
    miopenTensorDescriptor_t input_desc;
    cl_mem input_mem;
    miopenTensorDescriptor_t output_desc;
    cl_mem output_mem;
    float alpha, beta;
    miopenTensorDescriptor_t bnScaleBiasMeanVarDesc;
    cl_mem bnScale, bnBias;
};

static vx_status VX_CALLBACK validateScaleLayer(vx_node node, const vx_reference parameters[], vx_uint32 num, vx_meta_format metas[])
{

    // check tensor dimensions
    vx_enum type;
    vx_size num_dims;
    vx_size input_dims[4], output_dims[4];
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[0], VX_TENSOR_NUMBER_OF_DIMS, &num_dims, sizeof(num_dims)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[0], VX_TENSOR_DATA_TYPE, &type, sizeof(type)));
    if (num_dims != 4) return VX_ERROR_INVALID_DIMENSION;
    if (type != VX_TYPE_FLOAT32) return VX_ERROR_INVALID_TYPE;
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[0], VX_TENSOR_DIMS, input_dims, sizeof(input_dims)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[3], VX_TENSOR_NUMBER_OF_DIMS, &num_dims, sizeof(num_dims)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[3], VX_TENSOR_DATA_TYPE, &type, sizeof(type)));
    if (num_dims != 4) return VX_ERROR_INVALID_DIMENSION;
    if (type != VX_TYPE_FLOAT32) return VX_ERROR_INVALID_TYPE;
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[3], VX_TENSOR_DIMS, output_dims, sizeof(output_dims)));
    if (output_dims[3] != input_dims[3]) return VX_ERROR_INVALID_DIMENSION;
    if (output_dims[2] != input_dims[2]) return VX_ERROR_INVALID_DIMENSION;
    if (output_dims[1] != input_dims[1]) return VX_ERROR_INVALID_DIMENSION;
    if (output_dims[0] != input_dims[0]) return VX_ERROR_INVALID_DIMENSION;

    if (parameters[2]) {
        ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[2], VX_TENSOR_NUMBER_OF_DIMS, &num_dims, sizeof(num_dims)));
        ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[2], VX_TENSOR_DATA_TYPE, &type, sizeof(type)));
        if (num_dims != 1) return VX_ERROR_INVALID_DIMENSION;
        if (type != VX_TYPE_FLOAT32) return VX_ERROR_INVALID_TYPE;
        vx_size bias_dims[1];
        ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[2], VX_TENSOR_DIMS, bias_dims, sizeof(bias_dims)));
        if (bias_dims[0] != input_dims[2]) return VX_ERROR_INVALID_DIMENSION;
    }

    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[1], VX_TENSOR_NUMBER_OF_DIMS, &num_dims, sizeof(num_dims)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[1], VX_TENSOR_DATA_TYPE, &type, sizeof(type)));
    if (num_dims != 1) return VX_ERROR_INVALID_DIMENSION;
    if (type != VX_TYPE_FLOAT32) return VX_ERROR_INVALID_TYPE;
    vx_size scale_dims[1];
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[1], VX_TENSOR_DIMS, scale_dims, sizeof(scale_dims)));
    if (scale_dims[0] != input_dims[2]) return VX_ERROR_INVALID_DIMENSION;

    //output tensor configuration.
    type = VX_TYPE_FLOAT32;
    num_dims = 4;
    ERROR_CHECK_STATUS(vxSetMetaFormatAttribute(metas[3], VX_TENSOR_DATA_TYPE, &type, sizeof(type)));
    ERROR_CHECK_STATUS(vxSetMetaFormatAttribute(metas[3], VX_TENSOR_NUMBER_OF_DIMS, &num_dims, sizeof(num_dims)));
    ERROR_CHECK_STATUS(vxSetMetaFormatAttribute(metas[3], VX_TENSOR_DIMS, output_dims, sizeof(output_dims)));

    return VX_SUCCESS;
}

static vx_status VX_CALLBACK processScaleLayer(vx_node node, const vx_reference * parameters, vx_uint32 num)
{
    ScaleLayerLocalData * data = NULL;
    ERROR_CHECK_STATUS(vxQueryNode(node, VX_NODE_LOCAL_DATA_PTR, &data, sizeof(data)));
    miopenHandle_t miopenHandle = data->handle->miopen_handle;

    //miopen batch norm inference is combined for scale and  batchnorm. Scale is batchnorm withe null tensors for mean and variance.
    ERROR_CHECK_MIOPEN_STATUS(miopenBatchNormalizationForwardInference(miopenHandle, miopenBNSpatial, &data->alpha, &data->beta, data->input_desc, data->input_mem,
                                                                       data->output_desc, data->output_mem, data->bnScaleBiasMeanVarDesc, data->bnScale, data->bnBias, nullptr, nullptr, 0));

    return VX_SUCCESS;
}

static vx_status VX_CALLBACK initializeScaleLayer(vx_node node, const vx_reference *parameters, vx_uint32 num)
{
    ScaleLayerLocalData * data = new ScaleLayerLocalData;
    memset(data, 0, sizeof(*data));
    ERROR_CHECK_STATUS(createGraphHandle(node, &data->handle));

    //initialize input and output tensor descriptors.
    vx_size input_dims[4], output_dims[4];
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[0], VX_TENSOR_DIMS, input_dims, sizeof(input_dims)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[3], VX_TENSOR_DIMS, output_dims, sizeof(output_dims)));
    ERROR_CHECK_MIOPEN_STATUS(miopenCreateTensorDescriptor(&data->input_desc));
    ERROR_CHECK_MIOPEN_STATUS(miopenCreateTensorDescriptor(&data->bnScaleBiasMeanVarDesc));
    ERROR_CHECK_MIOPEN_STATUS(miopenCreateTensorDescriptor(&data->output_desc));
    ERROR_CHECK_MIOPEN_STATUS(miopenSet4dTensorDescriptor(data->input_desc, miopenFloat, input_dims[3], input_dims[2], input_dims[1], input_dims[0]));
    ERROR_CHECK_MIOPEN_STATUS(miopenSet4dTensorDescriptor(data->bnScaleBiasMeanVarDesc, miopenFloat, 1, input_dims[2], 1, 1));
    ERROR_CHECK_MIOPEN_STATUS(miopenSet4dTensorDescriptor(data->output_desc, miopenFloat, output_dims[3], output_dims[2], output_dims[1], output_dims[0]));
    ERROR_CHECK_MIOPEN_STATUS(miopenDeriveBNTensorDescriptor(data->bnScaleBiasMeanVarDesc, data->input_desc, miopenBNSpatial));

    data->alpha = 1; data->beta = 0;

    //input and output memory.
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[0], VX_TENSOR_BUFFER_OPENCL, &data->input_mem, sizeof(data->input_mem)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[1], VX_TENSOR_BUFFER_OPENCL, &data->bnScale, sizeof(data->bnScale)));
    if(parameters[2]){
        ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[2], VX_TENSOR_BUFFER_OPENCL, &data->bnBias, sizeof(data->bnBias)));
    }
    else{
        vx_context   vxContext = vxGetContext((vx_reference)node);
        cl_context context;
        ERROR_CHECK_STATUS(vxQueryContext(vxContext, VX_CONTEXT_ATTRIBUTE_AMD_OPENCL_CONTEXT, &context, sizeof(context)));
        cl_float pattern = 0;
        data->bnBias = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(float)*input_dims[2], NULL, NULL);
        cl_int err = clEnqueueFillBuffer(data->handle->cmdq, data->bnBias, &pattern, sizeof(cl_float), 0, input_dims[2], 0, NULL, NULL);
        if (err) return VX_FAILURE;
    }
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[3], VX_TENSOR_BUFFER_OPENCL, &data->output_mem, sizeof(data->output_mem)));

#if ENABLE_DEBUG_PRINT_DIMS
    std::cout << "scale input " << input_dims[3] << " " << input_dims[2] << " " << input_dims[1] << " " << input_dims[0] << " ";
    std::cout << " output " << output_dims[3] << " " << output_dims[2] << " " << output_dims[1] << " " << output_dims[0] << std::endl;
#endif

    ERROR_CHECK_STATUS(vxSetNodeAttribute(node, VX_NODE_LOCAL_DATA_PTR, &data, sizeof(data)));

    return VX_SUCCESS;
}

static vx_status VX_CALLBACK uninitializeScaleLayer(vx_node node, const vx_reference *parameters, vx_uint32 num)
{
    ScaleLayerLocalData * data = NULL;
    ERROR_CHECK_STATUS(vxQueryNode(node, VX_NODE_LOCAL_DATA_PTR, &data, sizeof(data)));
    ERROR_CHECK_MIOPEN_STATUS(miopenDestroyTensorDescriptor(data->input_desc));
    ERROR_CHECK_MIOPEN_STATUS(miopenDestroyTensorDescriptor(data->output_desc));
    ERROR_CHECK_MIOPEN_STATUS(miopenDestroyTensorDescriptor(data->bnScaleBiasMeanVarDesc));
    if(!parameters[4]){
        if(data->bnBias) {
            cl_int err = clReleaseMemObject(data->bnBias);
            if (err) return VX_FAILURE;
        }
    }
    if (data) {
        ERROR_CHECK_STATUS(releaseGraphHandle(node, data->handle));
        delete data;
    }
    return VX_SUCCESS;
}

vx_status publishScaleLayer(vx_context context)
{
    // add kernel to the context with callbacks
	vx_kernel kernel = vxAddUserKernel(context, "com.amd.nn_extension.scale_layer", VX_KERNEL_SCALE_LAYER_AMD, processScaleLayer, 4, validateScaleLayer, initializeScaleLayer, uninitializeScaleLayer);
    ERROR_CHECK_OBJECT(kernel);

    // enable OpenCL buffer access since the kernel_f callback uses OpenCL buffers instead of host accessible buffers
    vx_bool enableBufferAccess = vx_true_e;
    ERROR_CHECK_STATUS(vxSetKernelAttribute(kernel, VX_KERNEL_ATTRIBUTE_AMD_OPENCL_BUFFER_ACCESS_ENABLE, &enableBufferAccess, sizeof(enableBufferAccess)));

    // set kernel parameters
    ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 0, VX_INPUT, VX_TYPE_TENSOR, VX_PARAMETER_STATE_REQUIRED));
    ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 1, VX_INPUT, VX_TYPE_TENSOR, VX_PARAMETER_STATE_REQUIRED));
    ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 2, VX_INPUT, VX_TYPE_TENSOR, VX_PARAMETER_STATE_OPTIONAL));
    ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 3, VX_OUTPUT, VX_TYPE_TENSOR, VX_PARAMETER_STATE_REQUIRED));

    // finalize and release kernel object
    ERROR_CHECK_STATUS(vxFinalizeKernel(kernel));
    ERROR_CHECK_STATUS(vxReleaseKernel(&kernel));

    return VX_SUCCESS;
}

VX_API_ENTRY vx_node VX_API_CALL vxScaleLayer(vx_graph graph, vx_tensor inputs, vx_tensor scale, vx_tensor bias, vx_tensor output)
{
    vx_node node = NULL;
    vx_context context = vxGetContext((vx_reference)graph);
    if (vxGetStatus((vx_reference)context) == VX_SUCCESS) {
        vx_reference params[] = {
            (vx_reference)inputs,
            (vx_reference)scale,
            (vx_reference)bias,
            (vx_reference)output
        };
		node = createNode(graph, VX_KERNEL_SCALE_LAYER_AMD, params, sizeof(params) / sizeof(params[0]));
    }
    return node;
}
