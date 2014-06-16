/*
  ==============================================================================

    timer.h
    Created: 14 Jun 2014 4:23:45pm
    Author:  yvan vander sanden

  ==============================================================================
*/

#ifndef TIMER_H_INCLUDED
#define TIMER_H_INCLUDED

namespace YSE {
  namespace INTERNAL {
    
    class timer {
    public:
        virtual void callback() = 0;
      
        // start with millisecond precision
        void runEachMS(int interval);
      
        // start with second precision
        void runEachSec(int interval);
      
        void stop();
      
        bool isRunning() const;
      
        int interval();
      
      private:
      
    };



#endif  // TIMER_H_INCLUDED
