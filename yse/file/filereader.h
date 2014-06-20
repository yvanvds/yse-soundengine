/*
  ==============================================================================

    filereader.h
    Created: 20 Jun 2014 3:45:33pm
    Author:  yvan

  ==============================================================================
*/

#ifndef FILEREADER_H_INCLUDED
#define FILEREADER_H_INCLUDED

#include <string>
#include "../dsp/sample.hpp"

namespace YSE {
  namespace FILE {

    class fileReader {
    public:
      bool open(const std::wstring & filename);
      int getNumChannels();
      int getLengthInSamples();
      int fillBuffer(std::vector<AUDIOBUFFER> & buffer, int pos, int numSamples);

    };

  }
}



#endif  // FILEREADER_H_INCLUDED
