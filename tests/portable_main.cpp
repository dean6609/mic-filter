#include "dsp_tests.h"
#include <exception>
#include <iostream>

// Linux/macOS entry point: the processing engine only. Windows runs these plus its effect tests.
int main(){
    try{dspTests();rateTests();return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
