#include "DataControl.h"
#include <iostream>


bool DataControl::ready = false;
vector<string> DataControl::outputData = {""};

void DataControl::localCallback(freenect_device *ldev, void *data, uint32_t time) {
    //std::cout << "Getting stuff\n";
    DataControl::ready = false;


    uint16_t* dataC = (uint16_t*)data;
    string output("");

    //printf("%d", dataC);

    for (int i=0; i<640*480; i++)
        printf("(%04X:%d)", dataC[i], dataC[i]);

    //Ouput data to a vector
    //Should be removed later
    //////////////////////////////////
    for (int i=0; i<480; i++) {
        for (int j=0; j<640; j++) {
            output.insert(output.length(), to_string(dataC[i*j]) + ",");
        }
    }
    outputData.push_back(output);
    //////////////////////////////////
}


bool DataControl::errorCheck() {
    if (freenect_process_events(ctx) >= 0)
        return true;
    return false;
}

DataControl::DataControl()
{

    //Initialize the library
	int ret = freenect_init(&ctx, NULL);
	if (ret < 0) {
        DataControl::ready = false;
        return;
	}
	std::cout << ret << ": Initialized library.\n";

    //freenect_set_log_level(ctx, FREENECT_LOG_DEBUG);
    freenect_select_subdevices(ctx, FREENECT_DEVICE_CAMERA);

    //Check if there are devices to connect to
    ret = freenect_num_devices(ctx);
    if (ret < 0) {
        DataControl::ready = false;
        return;
	}
	else if (ret == 0) {
        DataControl::ready = false;
        std::cout << "No Devices were detected\n";
	}
	std::cout << ret << ": Found devices.\n";

    //Open the camera device
	ret = freenect_open_device(ctx, &dev, 0);
	if (ret < 0) {
		freenect_shutdown(ctx);
		DataControl::ready = false;
		return;
	}
	std::cout << ret << ": Device opened.\n";

    //Set the callback for retrieving data
    freenect_set_depth_callback(dev, localCallback);
    std::cout << "X: Callback set.\n";

    //Set the debth options
	ret = freenect_set_depth_mode( dev, freenect_find_depth_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_DEPTH_MM));
	if (ret < 0) {
		freenect_shutdown(ctx);
		freenect_close_device(dev);
		DataControl::ready = false;
		return;
	}
    std::cout << ret << ": Set depth mode.\n";

    //Start the data retrival
    ret = freenect_start_depth(dev);
    if (ret < 0) {
        freenect_close_device(dev);
		freenect_shutdown(ctx);
		DataControl::ready = false;
		return;
	}
	std::cout << ret << ": Started debth.\n";

    //Flag to indicated the device is ready and running
    DataControl::ready = true;
    std::cout << "X: Finished initializing the device\n";

}

//Closes down the connect when the object is destroyed
DataControl::~DataControl()
{
    std::cout << "X: Device is shutting down!\n";
    DataControl::ready = false;

    if (dev != nullptr) {
        freenect_stop_depth(dev);
        freenect_close_device(dev);
    }
    if (ctx != nullptr)
        freenect_shutdown(ctx);
}

