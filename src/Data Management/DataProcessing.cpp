#include "DataProcessing.h"
#include <chrono>
#include <utility>


const int minDistance = -10;
const float scaleFactor = .0021;
//half width and height
const int w2 = 320;
const int h2 = 240;
const int MAX_SIZE = 1000;


DataProcessing::DataProcessing() { Log::outln("Data processing init."); };

DataProcessing::~DataProcessing() {
	Log::outln("Finished processing.");

	delete thread1;
	delete fpsc;

};

void DataProcessing::startThread() {
	thread1 = new std::thread(&DataProcessing::epiphanyRun, this);
	fpsc = new std::thread(&DataProcessing::fpsCounter, this);
}

//Need to get header file to compile on this system
//Make sure Home is set: EPIPHANY_HDF=”${EPIPHANY_HOME}/bsps/parallella/parallella.xml”
int DataProcessing::epiphanyInit() {
	std::cout << "Starting Epiphany device!" << std::endl;

	int ret = 0;
	e_platform_t platform;
	dev = new e_epiphany_t;

	e_set_host_verbosity(H_D0);

	ret = e_init(NULL);
	if (ret == E_OK)
		std::cout << "Init sucessfull!" << std::endl;
	else {
		std::cout << "Failed to start!" << std::endl;
		e_finalize();
		return -1;
	}

	ret = e_reset_system();
	if (ret == E_OK)
		std::cout << "Reset!" << std::endl;
	else {
		std::cout << "Couldn't reset?" << std::endl;
		e_finalize();
		return -1;
	}

	ret = e_get_platform_info(&platform);
	if (ret == E_OK)
		std::cout << "Got the info!" << std::endl;
	else {
		std::cout << "No info :(" << std::endl;
		e_finalize();
		return -1;
	}

	std::cout << "Starting Epiphany cores!" << std::endl;
	std::cout << "-Version: " << platform.version << std::endl;
	std::cout << "-Coordinates: (" << platform.row << "," << platform.col << ")" << std::endl;
	std::cout << "-Area: " << platform.rows << "x" << platform.cols << std::endl;

	//Opens the group. Get row and col info from platform
	ret = e_open(dev, 0, 0, 4, 4);
	if (ret == E_OK)
		std::cout << "Opened Fine!" << std::endl;
	else {
		std::cout << "Did not open!" << std::endl;
		e_finalize();
		return -1;
	}

	ret = e_load_group("e_calc.srec", dev, 0, 0, 4, 4, E_FALSE);
	if (ret == E_OK)
		std::cout << "Loaded Program!" << std::endl;
	else {
		std::cout << "Failed to load Program!" << std::endl;
		e_finalize();
		return -1;
	}

	std::cout << "Setting shared memory to all zeros" << std::endl;
	e_mem_t mem;
	int zero = 0;
	int meg32 = 1000*1000*8;

	ret = e_alloc(&mem, 0, meg32*sizeof(int));
	if (ret == E_OK)
		std::cout << "Allocated wipe memory!" << std::endl;
	else {
		std::cout << "Failed to alocate wipe memory!" << std::endl;
		e_finalize();
		return -1;
	}

	//Writes zero value to each value the size of an int, for 32 Mb
	for (int i=0; i<meg32; i++) {
		ret = e_write(&mem, 0, 0, i*4, &zero, 4);
	}

	return 0;
}

void DataProcessing::epiphanyRun() {
	e_mem_t mbuf;

	std::cout << "\n\nStarting Data processing with epiphany cores!\n";

	//Alocate memory for all sixteen cores
	int data_size = sizeof(PointKey)*MAX_SIZE;
	int header_size = sizeof(int)*3;
	int num_cores = 16;

	//std::cout << "Data size = " << header_size << std::endl;

	int ret = e_alloc(&mbuf, 0, (header_size + data_size)*num_cores);
	if (ret == E_OK)
		std::cout << "Allocated Memory!" << std::endl;
	else {
		std::cout << "Failed to alocate Memory!" << std::endl;
		e_finalize();
		return;
	}

	//Sets the host status codes to 2
	int init_code = 2;
	for (int i=0; i<num_cores; i++) {
		int offset = (header_size + data_size)*i;
		ret = e_write(&mbuf, 0, 0, offset, &init_code, 4);

		if (ret < 4)
			std::cout << "Did not write the start codes correctly" << std::endl;
	}

	ret = e_start_group(dev);
	//ret = e_start(dev, 0, 0);
	//ret = e_start(dev, 0, 1);
	//ret = e_start(dev, 0, 2);
	if (ret == E_OK)
		std::cout << "Started!" << std::endl;
	else {
		std::cout << "Failed to start!" << std::endl;
		e_finalize();
		return;
	}

	bool not_done = true;
	bool shutdown = false;
	bool wait = false;
	int num_errors = 0;
	PointKey *pArray = new PointKey[MAX_SIZE];

	//Ststus code loggers
	int code_0 = 0;
	int code_1 = 0;
	int code_2 = 0;
	int code_3 = 0;
	int code_7 = 0;
	int code_8 = 0;
	int code_9 = 0;
	int code_other = 0;

	//Pregenerate the offset codes
	int offset_codes[16];
	for (int i=0; i<16; i++) {
		offset_codes[i] = (header_size + data_size)*i;
		std::cout << "Offset " << i << ": " << std::hex << offset_codes[i] << std::dec << std::endl;
	}

	long long timer1 = 0;
	long counter1 = 0;
	long long timer2 = 0;
	long counter2 = 0;
	//long long timer3 = 0;
	//long counter3 = 0;

	while (not_done) {
		wait = false;
		//std::cout << "DISTANCE: " << DataControl::buff.getDistance() << std::endl;


		auto start1 = std::chrono::high_resolution_clock::now();
		//Reads through the status codes of the cores
		for (int i=0; i<num_cores; i++) {
			int core_code = 0;
			e_read(&mbuf, 0, 0, offset_codes[i] + 4, &core_code, 4);
		//	std::cout << "CORE: "<< i << "|" << core_code << "  ";

			int init_code = 0;
			switch (core_code) {
				case 0:
					code_0++;
					if (!shutdown) {
						//Bad error, faulty core
						num_errors++;
						init_code = 0;
						e_write(&mbuf, 0, 0, offset_codes[i], &init_code, 4);
					}
					break;
				case 1:
					code_1++;
					break;
				case 2:
					init_code = 3;
					e_write(&mbuf, 0, 0, offset_codes[i], &init_code, 4);
					code_2++;
					break;
				case 3:
					code_3++;
					break;
				case 7:
					code_7++;
					break;
				case 8:
					num_errors++;
					init_code = 2;
					e_write(&mbuf, 0, 0, offset_codes[i], &init_code, 4);
					code_8++;
					break;
				case 9:
					wait = true;
					code_9++;
					break;
				default:
					code_other++;
					break;
			}
		}
		//std::cout << std::endl;
		auto finish1 = std::chrono::high_resolution_clock::now();
		timer1 += std::chrono::duration_cast<std::chrono::nanoseconds>(finish1-start1).count();
		counter1++;

		auto start2 = std::chrono::high_resolution_clock::now();
		int num_done = 0;
		//Reads through the status codes of the cores
		for (int i=0; i<num_cores; i++) {
			int host_code = 0;
			//auto start3 = std::chrono::high_resolution_clock::now();
			e_read(&mbuf, 0, 0, offset_codes[i], &host_code, 4);
			//auto finish3 = std::chrono::high_resolution_clock::now();
		//	timer3 += std::chrono::duration_cast<std::chrono::nanoseconds>(finish3-start3).count();
			//counter3++;
			int size = 0;
		//	std::cout << "HOST: "<< i << "|" << host_code << "  ";

			int init_code = 0;
			switch (host_code) {
				case 0:
					num_done++;
					break;
				case 1:
					break;
				case 2:
					{
						if (shutdown) {
							//if shutdown in progress, set host status code to zero instead of writing data
							init_code = 0;
							//e_write(&mbuf, 0, 0, offset_codes[i], &init_code, 4);
						}
						else
							init_code = 1;
						//int distance = DataControl::buff.getDistance();


						//if( distance > 0 ) {
						//	if( distance > MAX_SIZE )
						//		distance = MAX_SIZE;


							  //std::cout << "In "  << std::endl;
							size = DataControl::buff.get(pArray, MAX_SIZE);

							if (size <= 0 )
								continue;


							e_write(&mbuf, 0, 0, offset_codes[i] + 8, &size, 4);
							e_write(&mbuf, 0, 0, offset_codes[i] + 12, pArray, sizeof(PointKey)*size);
							e_write(&mbuf, 0, 0, offset_codes[i], &init_code, 4);

						//}
						if(DataControl::ready) {
							continue;
						}
						else {
							std::cout << "Shutting down...\n";
							shutdown = true;
						}

						break;
					}
				case 3:
					size = 0;
					//std::cout << "Witing data to the cloud.\n";
					e_read(&mbuf, 0, 0, offset_codes[i]+8, &size, 4);
					e_read(&mbuf, 0, 0, offset_codes[i]+12, pArray, sizeof(PointKey)*size);

					for(int j=0; j<size; j++) {
						if (pArray[j].z != 0);
							store.writeToFileBuffer(pArray[j].x, pArray[j].y, pArray[j].z);
					}

					init_code = 2;
					e_write(&mbuf, 0, 0, offset_codes[i], &init_code, 4);
					break;
				default:
					break;

			}
		}
		//std::cout << std::endl;
		auto finish2 = std::chrono::high_resolution_clock::now();
		timer2 += std::chrono::duration_cast<std::chrono::nanoseconds>(finish2-start2).count();
		counter2++;


		if (num_done == num_cores && !wait)
			not_done = false;
	}


	std::cout << "Computation Done." << std::endl;
	std::cout << "(ERROR: " << num_errors << ")" << std::endl;
	std::cout << "STATUS-0: " << code_0 << std::endl;
	std::cout << "STATUS-1: " << code_1 << std::endl;
	std::cout << "STATUS-2: " << code_2 << std::endl;
	std::cout << "STATUS-3: " << code_3 << std::endl;
	std::cout << "STATUS-7: " << code_7 << std::endl;
	std::cout << "STATUS-8: " << code_8 << std::endl;
	std::cout << "STATUS-9: " << code_9 << std::endl;
	std::cout << "STATUS-OTHER: " << code_other << std::endl;
	std::cout << "TIME ONE: " << (timer1/counter1) << std::endl;
	std::cout << "TIME TWO: " << (timer2/counter2) << std::endl;
//	std::cout << "TIME THREE: " << (timer3/counter3) << std::endl;


	for (int i=1; i<=num_cores; i++) {
		e_mem_t dbug_size;
		e_mem_t dbug_text;

		int base = 0x00100000*i;
		int text_size = 0;
		int num = 0;
		int offset = (header_size + data_size)*(i-1);

		e_alloc(&dbug_size, base, sizeof(int)*2);
		e_read(&dbug_size, 0, 0, 0, &text_size, sizeof(int));
		e_read(&dbug_size, 0, 0, 4, &num, sizeof(int));

		if (text_size == 0) {
			int status = 0;
			e_read(&mbuf, 0, 0, offset, &status, sizeof(int));
			std::cout << "No text from Core " << num << " (CODE: " << status << ")" << std::endl;
			e_free(&dbug_size);
			e_free(&dbug_text);
			continue;
		}


		e_alloc(&dbug_text, base + 8, sizeof(char)*text_size);
		char text[text_size];
		e_read(&dbug_text, 0, 0, 0, &text, sizeof(char)*text_size);
		int status_c = 0;
		e_read(&mbuf, 0, 0, offset+4, &status_c, sizeof(int));
		int status_h = 0;
		e_read(&mbuf, 0, 0, offset, &status_h, sizeof(int));

		std::cout << "//////Core " << num << " Debug//////" << std::endl;
		std::cout << "Status code host = " << status_h << std::endl;
		std::cout << "Status code core = " << status_c << std::endl;
		std::cout << "Text size=" << text_size << std::endl;

		for (int i=0; i<text_size; i++)
			std::cout << text[i];
		std::cout << std::endl;

		e_free(&dbug_size);
		e_free(&dbug_text);

	}

	e_free(&mbuf);

	std::cout << "Done running\n\n" << std::endl;
}

void DataProcessing::epiphanyClose() {
	int ret = 0;


	ret = e_close(dev);
	Log::outln(ret,"Core group closed.");
	ret = e_finalize();
	Log::outln(ret,"Epiphany closed.");

	delete dev;
}


void DataProcessing::fpsCounter() {

	while (DataControl::ready) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));

		DataControl::buff.printSize();
		std::cout << " FPS:" << DataControl::frames <<  " Store: " << store.getBufferLength()/1000 << "k" << std::endl;
		DataControl::frames = 0;
		//DataControl::ready = false;

	}
}

void DataProcessing::join() {
	Log::outln("Waiting for threads...");
	thread1->join();
	fpsc->join();
	Log::outln("All threads finished.");
};
