#pragma once
#include "basePage.h"

class DemoVirtualIO : public basePage {
public:
  DemoVirtualIO();
 ~DemoVirtualIO();

  virtual void ExplainDemo();

private:
  void One();
  void Two();
  void Three();

  int loadToBuffer(const char * file, char ** buffer);

  char * fileBuffer1;
  char * fileBuffer2;
  char * fileBuffer3;

  YSE::BufferIO FileBuffer;

  YSE::sound s1, s2, s3;
};