#include "stdafx.h"
#include "Demo11_VirtualIO.h"
#include <fstream>

DemoVirtualIO::DemoVirtualIO() {
  SetTitle("Virtual IO with buffer manager");
  AddAction('1', "Toggle Sound 1", std::bind(&DemoVirtualIO::One, this));
  AddAction('2', "Toggle Sound 2", std::bind(&DemoVirtualIO::Two, this));
  AddAction('3', "Toggle Sound 3", std::bind(&DemoVirtualIO::Three, this));

  // fill buffermanager
  int f1 = loadToBuffer("..\\TestResources\\countdown.ogg", &fileBuffer1);
  int f2 = loadToBuffer("..\\TestResources\\contact.ogg", &fileBuffer2);
  int f3 = loadToBuffer("..\\TestResources\\flies.ogg", &fileBuffer3);
  
  FileBuffer.AddBuffer("Sound1", fileBuffer1, f1);
  FileBuffer.AddBuffer("Sound2", fileBuffer2, f2);
  FileBuffer.AddBuffer("Sound3", fileBuffer3, f3);
  FileBuffer.SetActive(true);

  s1.create("Sound1", nullptr, true);
  s2.create("Sound2", nullptr, true);
  s3.create("Sound3", nullptr, true);
}

void DemoVirtualIO::ExplainDemo() {
  std::cout << "This demo loads files into byte buffers which are passed "
    << "to the YSE::BufferIO object. Once that is activated, YSE will play audio "
    << "from these buffers instead of real files." << std::endl;
  std::cout << "This approach is an implementation of a virtual IO system and can "
    << "be useful with android or game engines." << std::endl;
}

DemoVirtualIO::~DemoVirtualIO() {
  s1.stop();
  s2.stop();
  s3.stop();

  FileBuffer.SetActive(false);

  delete[] fileBuffer1;
  delete[] fileBuffer2;
  delete[] fileBuffer3;
}

void DemoVirtualIO::One() {
  s1.toggle();
}

void DemoVirtualIO::Two() {
  s2.toggle();
}

void DemoVirtualIO::Three() {
  s3.toggle();
}

int DemoVirtualIO::loadToBuffer(const char * file, char ** buffer) {
  std::ifstream is(file, std::ifstream::binary);
  if (is) {
    is.seekg(0, is.end);
    int length = (int)is.tellg();
    is.seekg(0, is.beg);

    *buffer = new char[length];
    is.read(*buffer, length);
    is.close();

    return length;
  }
  return 0;
}
