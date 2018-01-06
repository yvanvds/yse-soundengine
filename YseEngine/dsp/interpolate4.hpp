/*
  ==============================================================================

    interpolate4.hpp
    Created: 4 Sep 2015 11:53:08am
    Author:  yvan

  ==============================================================================
*/

#ifndef INTERPOLATE4_HPP_INCLUDED
#define INTERPOLATE4_HPP_INCLUDED

#include "buffer.hpp"

namespace YSE {
  namespace DSP {

    class API interpolate4 {
    public:
      interpolate4();

      interpolate4 & source(buffer & data);
      buffer * source();

      interpolate4 & onset(Int value);
      Int onset();

      buffer & operator()(YSE::DSP::buffer & in);

    private:
      buffer * data;
      buffer out;
      aInt parmOnset;
    };

  }
}



#endif  // INTERPOLATE4_HPP_INCLUDED
