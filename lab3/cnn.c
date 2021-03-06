#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include "cnn.h"
#include <CL/cl.h>
#include "kernel_cl.h"

#define GLOBAL_WORK_ITEMS 32


// Sequential CNN implementation
void CONV(float Cout[NUM][IMROW][IMROW], float Cin[NUM][INIMROW][INIMROW],
          float weight[NUM][NUM][KERNEL][KERNEL], float bias[NUM])
{
	for(int i=0; i<NUM; i++) {
		for(int h=0; h<IMROW; h++) {
			for(int w=0; w<IMROW; w++)
				Cout[i][h][w] = bias[i];
		}
	}
	for(int i=0; i<NUM; i++) {
		for(int j=0; j<NUM; j++) {
			for(int h=0; h<IMROW; h++) {
				for(int w=0; w<IMROW; w++) {
					for(int p=0; p<KERNEL; p++) {
						for(int q=0; q<KERNEL; q++)
							Cout[i][h][w] += weight[i][j][p][q]*Cin[j][1*h+p][1*w+q];
					}
				}
			}
		}
	}
}


int main()
{
	static float Cout[NUM][IMROW][IMROW];
	static float Cin[NUM][INIMROW][INIMROW];
	static float weight[NUM][NUM][KERNEL][KERNEL];
	static float bias[NUM];

	LoadData(Cin, weight, bias);

	// Use this to check the output of each API call
    cl_int status;

    // 1) GET PLATFORMS
    // Call 1: Retrieve the number of platforms
    cl_uint numPlatforms = 0;
    status = clGetPlatformIDs(0, NULL, &numPlatforms);
    // Allocate enough space for numPlatforms platforms
    cl_platform_id *platforms = NULL;
    platforms = (cl_platform_id*)malloc(numPlatforms * sizeof(cl_platform_id));
    // Call 2: Get the actual Platforms
    status = clGetPlatformIDs(numPlatforms, platforms, NULL);

    // 2) GET PLATFORM INFO
	int platform_index = -1;
	int i;
	for (i = 0; i < numPlatforms; i++)
	{
		char vendor[128];
		clGetPlatformInfo (platforms[i], CL_PLATFORM_VENDOR, sizeof(vendor), vendor, NULL);
		char vendorF[7];
		memcpy((void*)vendorF, (void*)vendor, 6);
		vendorF[7] = '\0';
		//Print the patform vender
		fprintf(stderr, "%s\n", vendorF);
		//If it is an Intel platform set this to be the patform indez
		if (strcmp(vendorF, "Intel(") == 0)
		{
			platform_index = i;
			break;
		}
	}
	//If you dont find an Intel platform exit with an error
	if (platform_index == -1){
		printf("Didn't find GPU platform!\n");
		exit(1);
	}

	// 3) GET DEVICES
	// Call 1: Retrieve the number of devices
    cl_uint numDevices = 0;
    status = clGetDeviceIDs(platforms[platform_index], CL_DEVICE_TYPE_ALL, 0, NULL, &numDevices);
	printf("#devices: %d, status %d\n", numDevices, status);
    // Allocate enough space for numDevices devices
    cl_device_id *devices;
    devices = (cl_device_id*)malloc(numDevices * sizeof(cl_device_id));
    // Call 2: Get the actual devices
	status = clGetDeviceIDs(platforms[platform_index], CL_DEVICE_TYPE_ALL,        
	        numDevices, devices, NULL);

	// 4) CREATE CONTEXT, and associate it with the devices
    cl_context context;
    context = clCreateContext(NULL, numDevices, devices, NULL, 
        NULL, &status);

    // 5) CREATE COMMAND QUEUE, and associate it with the first device 
    cl_command_queue cmdQueue;
    cmdQueue = clCreateCommandQueue(context, devices[0], CL_QUEUE_PROFILING_ENABLE, &status);

    // 6) CREATE BUFFERS
    // Call 1: create buffer for Cout
    cl_mem bufCout;
    size_t cout_size = NUM * IMROW * IMROW * sizeof(float);
    bufCout = clCreateBuffer(context, CL_MEM_READ_ONLY, cout_size, NULL, &status);

    // Call 2: create buffer for Cin
    cl_mem bufCin;
    size_t cin_size = NUM * INIMROW * INIMROW * sizeof(float);
    bufCin = clCreateBuffer(context, CL_MEM_READ_ONLY, cin_size, NULL, &status);

	// Call 3: create buffer for weight
    cl_mem bufWeight;
    size_t weight_size = NUM * NUM * KERNEL * KERNEL * sizeof(float);
    bufWeight = clCreateBuffer(context, CL_MEM_READ_ONLY, weight_size, NULL, &status);

	// Call 4: create buffer for bias
    cl_mem bufBias;
    size_t bias_size = NUM * sizeof(float);
    bufBias = clCreateBuffer(context, CL_MEM_READ_ONLY, bias_size, NULL, &status);

    // 7) WRITE BUFFERS
    // Call 1: write Cin to bufCin
    status = clEnqueueWriteBuffer(cmdQueue, bufCin, CL_FALSE, 
        0, cin_size, Cin, 0, NULL, NULL);

    // Call 2: write weight to bufWeight
    status = clEnqueueWriteBuffer(cmdQueue, bufWeight, CL_FALSE, 
        0, weight_size, weight, 0, NULL, NULL);

    // Call 3: write bias to bufBias
    status = clEnqueueWriteBuffer(cmdQueue, bufBias, CL_FALSE, 
        0, bias_size, bias, 0, NULL, NULL);

    // 8) CREATE KERNEL
    // Create a program from kernel_cl.h source code
    cl_program program = clCreateProgramWithSource(context, 1, 
        (const char**)&kernel_cl, NULL, &status);
    // Build (compile) the program for the device
    status = clBuildProgram(program, numDevices, devices, 
        NULL, NULL, NULL);
    // Create a kernel from the CONV function in the program (kernel_cl.h)
    cl_kernel kernel;
    kernel = clCreateKernel(program, "CONV", &status);
    // Associate the input and output buffers with the kernel 
    status = clSetKernelArg(kernel, 0, sizeof(cl_mem), &bufCout);
    status = clSetKernelArg(kernel, 1, sizeof(cl_mem), &bufCin);
    status = clSetKernelArg(kernel, 2, sizeof(cl_mem), &bufWeight);
    status = clSetKernelArg(kernel, 3, sizeof(cl_mem), &bufBias);


    // 9) EXECUTE KERNEL
    // Define an index space (global work size) of work items for execution. 
    size_t globalWorkSize[1];   
    // total number of work items
    globalWorkSize[0] = GLOBAL_WORK_ITEMS;
    //Make sure all queued events are finished
    clFinish(cmdQueue);
    cl_event event;
    // Execute the kernel for execution
    status = clEnqueueNDRangeKernel(cmdQueue, kernel, 1, NULL, globalWorkSize, NULL, 0, NULL, &event);
    //Make sure kernel execution has finished
    clWaitForEvents(1, &event);

    //EXECUTION TIME
    cl_ulong time_start, time_end;
	double total_time;
	clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START, sizeof(time_start), &time_start, NULL);
	clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END, sizeof(time_end), &time_end, NULL);
	total_time = time_end - time_start;
	printf("\nExecution time in seconds = %0.3f sec\n", (total_time / 1000000000.0));

    // 10) READ BUFFER
    // Read the device output buffer to the host output array
    clEnqueueReadBuffer(cmdQueue, bufCout, CL_TRUE, 0, cout_size, Cout, 0, NULL, NULL);

    // 11) VERIFY RESULT
    int error = Verify(Cout);
	if(error != 0)
		fprintf(stderr, "error ocurrs %d\n", error);
	else
		fprintf(stderr, "all right!\n");

    // 12) FREE OpenCL RESOURCES
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(cmdQueue);
    clReleaseMemObject(bufCout);
    clReleaseMemObject(bufCin);
    clReleaseMemObject(bufWeight);
    clReleaseMemObject(bufBias);
    clReleaseContext(context);

	return 0;
}

