/*
  ==============================================================================

    note.h
    Created: 4 Apr 2014 10:22:39am
    Author:  yvan vander sanden

  ==============================================================================
*/

#ifndef NOTE_H_INCLUDED
#define NOTE_H_INCLUDED

#include "../classes.hpp"
#include "../headers/defines.hpp"

namespace YSE {
    
    namespace MUSIC {
        class pNote;

        class API note {
        private:
            Flt pitch  ;
            Flt volume ;
            Flt length ;
            Int channel;

        public:
            note(Flt pitch = 60.f, Flt volume = 1.f, Flt length = 0.f, Int channel = 1);
            note(const note & object);
            
            note & set       (Flt pitch, Flt volume = 1.f, Flt length = 0.f, Int channel = 1);
            note & setPitch  (Flt value);
            note & setVolume (Flt value);
            note & setLength (Flt value);
            note & setChannel(Int value);
            
            Flt getPitch  () const;
            Flt getVolume () const;
            Flt getLength () const;
            Int getChannel() const;

            bool update(); // returns false if note wants to stop
            bool update(Flt delta); // a special, more accurate version of update. This can only be called at DSP level
            
            // these operators work on pitch only,
            // volume is never changed
            note & operator+=(const note& object);
            note & operator-=(const note& object);
            note & operator*=(const note& object);
            note & operator/=(const note& object);

            void operator=(const pNote& other);
            
            note & operator+=(Flt pitch);
            note & operator-=(Flt pitch);
            note & operator*=(Flt pitch);
            note & operator/=(Flt pitch);
            
            Bool operator==(const note& object);
            Bool operator!=(const note& object);
            Bool operator<(const note& object);
            Bool operator>(const note& object);
            Bool operator<=(const note& object);
            Bool operator>=(const note& object);
            
            Bool operator==(Flt pitch);
            Bool operator!=(Flt pitch);
            Bool operator<(Flt pitch);
            Bool operator>(Flt pitch);
            Bool operator<=(Flt pitch);
            Bool operator>=(Flt pitch);
            
            friend note operator+(const note &n, Flt pitch);
            friend note operator-(const note &n, Flt pitch);
            friend note operator*(const note &n, Flt pitch);
            friend note operator/(const note &n, Flt pitch);
            
            friend note operator+(Flt pitch, const note &n);
            friend note operator-(Flt pitch, const note &n);
            friend note operator*(Flt pitch, const note &n);
            friend note operator/(Flt pitch, const note &n);
            
            friend note operator+(const note &n1, const note &n2);
            friend note operator-(const note &n1, const note &n2);
            friend note operator*(const note &n1, const note &n2);
            friend note operator/(const note &n1, const note &n2);


        };
        
    }
    
}




#endif  // NOTE_H_INCLUDED
