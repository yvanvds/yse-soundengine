/*
  ==============================================================================

    chord.h
    Created: 4 Apr 2014 10:22:53am
    Author:  yvan vander sanden

  ==============================================================================
*/

#ifndef CHORD_H_INCLUDED
#define CHORD_H_INCLUDED

#include "../classes.hpp"
#include "note.hpp"
#include <vector>


namespace YSE {
    
  namespace MUSIC {

    /**
     *  @brief An unordered collection of ``note`` objects sounding together.
     *
     *  Build a chord additively or pass several notes to the variadic
     *  constructor. The arithmetic operators add notes to / remove notes from
     *  the chord, and ``transpose`` shifts every note's pitch by a fixed
     *  amount.
     */
    class API chord {
    private:
      std::vector<note> notes;

    public:

      chord();
      chord(const chord & object);

      /**
       *  @brief Variadic constructor — pass ``count`` followed by ``count`` ``note`` values.
       *
       *  Example: ``chord c(3, note(60), note(64), note(67));`` builds a C major triad.
       */
      chord(UInt count, ...);

      /** @brief Number of notes in the chord. */
      UInt elms();

      /** @brief Shift every note's pitch by ``value``. */
      chord & transpose(Flt value);

      /** @brief Append every note of ``object`` to this chord. */
      chord & operator+=(const chord& object);

      /** @brief Remove notes that also appear in ``object``. */
      chord & operator-=(const chord& object);

      /** @brief Append a single note. */
      chord & operator+=(const note& object);

      /** @brief Remove a single note. */
      chord & operator-=(const note& object);

      /** @brief Whether the two chords hold the same notes. */
      Bool operator==(const chord& object);
      /** @brief Inequality. */
      Bool operator!=(const chord& object);

      /// @cond INTERNAL
      friend chord operator+(const chord &c, const note &n);
      friend chord operator-(const chord &c, const note &n);

      friend chord operator+(const chord &c1, const chord &c2);
      friend chord operator-(const chord &c1, const chord &c2);

      friend chord operator+(const note &n, const chord &c);
      friend chord operator-(const note &n, const chord &c);
      /// @endcond
    };

    /** @brief Chord with an extra note appended. */
    chord operator+(const chord &c, const note &n);
    /** @brief Chord with the given note removed. */
    chord operator-(const chord &c, const note &n);

    /** @brief Concatenation of two chords. */
    chord operator+(const chord &c1, const chord &c2);
    /** @brief Notes of ``c1`` minus notes of ``c2``. */
    chord operator-(const chord &c1, const chord &c2);

    /** @brief Chord built from a single note plus the notes of ``c``. */
    chord operator+(const note &n, const chord &c);
    /** @brief Notes of ``c`` minus the single note ``n``. */
    chord operator-(const note &n, const chord &c);

  }
    
}



#endif  // CHORD_H_INCLUDED
