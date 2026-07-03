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

    /**
     *  @brief A single musical event — pitch, volume, length, and MIDI channel.
     *
     *  ``note`` is the fundamental unit for the music subsystem: chords are
     *  collections of notes, motifs are sequences of pitched notes
     *  (``pNote``), and players generate notes within a ``scale`` constraint.
     *
     *  The arithmetic operators (``+`` / ``-`` / ``*`` / ``/``) operate on
     *  ``pitch`` only; ``volume``, ``length``, and ``channel`` are preserved.
     *
     *  @see YSE::MUSIC::pNote
     *  @see YSE::MUSIC::chord
     *  @see YSE::scale
     */
    class API note {
    private:
      Flt pitch;
      Flt volume;
      Flt length;
      Int channel;

    public:
      /**
       *  @brief Construct a note.
       *
       *  @param pitch   MIDI pitch (60 = middle C).
       *  @param volume  Velocity in [0.0, 1.0].
       *  @param length  Duration in seconds. 0 means use the default length.
       *  @param channel MIDI channel (1-16).
       */
      note(Flt pitch = 60.f, Flt volume = 1.f, Flt length = 0.f, Int channel = 1);
      note(const note& object);
      note& operator=(const note&) = default;

      /** @brief Replace all four fields in one call. */
      note& set(Flt pitch, Flt volume = 1.f, Flt length = 0.f, Int channel = 1);

      /** @brief Set the MIDI pitch. */
      note& setPitch(Flt value);

      /** @brief Set the velocity. */
      note& setVolume(Flt value);

      /** @brief Set the duration in seconds. */
      note& setLength(Flt value);

      /** @brief Set the MIDI channel. */
      note& setChannel(Int value);

      /** @brief Current pitch. */
      Flt getPitch() const;

      /** @brief Current velocity. */
      Flt getVolume() const;

      /** @brief Current duration. */
      Flt getLength() const;

      /** @brief Current MIDI channel. */
      Int getChannel() const;

      /** @brief Advance the note one tick. Returns ``false`` once the note has finished. */
      bool update();

      /**
       *  @brief More precise variant of ``update`` driven by an explicit delta.
       *  @note Audio-thread only.
       */
      bool update(Flt delta);

      /** @brief Add another note's pitch (pitch only). */
      note& operator+=(const note& object);

      /** @brief Subtract another note's pitch (pitch only). */
      note& operator-=(const note& object);

      /** @brief Multiply pitch by another note's pitch. */
      note& operator*=(const note& object);

      /** @brief Divide pitch by another note's pitch. */
      note& operator/=(const note& object);

      /** @brief Copy from a positioned note (drops the position field). */
      void operator=(const pNote& other);

      /** @brief Add a pitch value. */
      note& operator+=(Flt pitch);

      /** @brief Subtract a pitch value. */
      note& operator-=(Flt pitch);

      /** @brief Multiply pitch by a value. */
      note& operator*=(Flt pitch);

      /** @brief Divide pitch by a value. */
      note& operator/=(Flt pitch);

      /** @brief Equality on pitch. */
      Bool operator==(const note& object);
      /** @brief Inequality on pitch. */
      Bool operator!=(const note& object);
      /** @brief Order by pitch. */
      Bool operator<(const note& object);
      /** @brief Order by pitch. */
      Bool operator>(const note& object);
      /** @brief Order by pitch. */
      Bool operator<=(const note& object);
      /** @brief Order by pitch. */
      Bool operator>=(const note& object);

      /** @brief Compare pitch against a value. */
      Bool operator==(Flt pitch);
      /** @brief Compare pitch against a value. */
      Bool operator!=(Flt pitch);
      /** @brief Compare pitch against a value. */
      Bool operator<(Flt pitch);
      /** @brief Compare pitch against a value. */
      Bool operator>(Flt pitch);
      /** @brief Compare pitch against a value. */
      Bool operator<=(Flt pitch);
      /** @brief Compare pitch against a value. */
      Bool operator>=(Flt pitch);

      /// @cond INTERNAL
      // Friend declarations grant access to ``pitch`` / ``volume`` to the
      // namespace-scope free operators below. The friend forms are
      // hidden from Doxygen — they would otherwise be mistaken for class
      // members.
      friend note operator+(const note& n, Flt pitch);
      friend note operator-(const note& n, Flt pitch);
      friend note operator*(const note& n, Flt pitch);
      friend note operator/(const note& n, Flt pitch);

      friend note operator+(Flt pitch, const note& n);
      friend note operator-(Flt pitch, const note& n);
      friend note operator*(Flt pitch, const note& n);
      friend note operator/(Flt pitch, const note& n);

      friend note operator+(const note& n1, const note& n2);
      friend note operator-(const note& n1, const note& n2);
      friend note operator*(const note& n1, const note& n2);
      friend note operator/(const note& n1, const note& n2);
      /// @endcond
    };

    /** @brief Add a pitch value to a note (pitch only). */
    note operator+(const note& n, Flt pitch);
    /** @brief Subtract a pitch value from a note (pitch only). */
    note operator-(const note& n, Flt pitch);
    /** @brief Multiply a note's pitch by a value. */
    note operator*(const note& n, Flt pitch);
    /** @brief Divide a note's pitch by a value. */
    note operator/(const note& n, Flt pitch);

    /** @brief Add a note's pitch to a value. The result carries the note's velocity. */
    note operator+(Flt pitch, const note& n);
    /** @brief Subtract a note's pitch from a value. The result carries the note's velocity. */
    note operator-(Flt pitch, const note& n);
    /** @brief Multiply a value by a note's pitch. The result carries the note's velocity. */
    note operator*(Flt pitch, const note& n);
    /** @brief Divide a value by a note's pitch. The result carries the note's velocity. */
    note operator/(Flt pitch, const note& n);

    /** @brief Add two notes' pitches (pitch only). */
    note operator+(const note& n1, const note& n2);
    /** @brief Subtract two notes' pitches (pitch only). */
    note operator-(const note& n1, const note& n2);
    /** @brief Multiply two notes' pitches (pitch only). */
    note operator*(const note& n1, const note& n2);
    /** @brief Divide two notes' pitches (pitch only). */
    note operator/(const note& n1, const note& n2);

  } // namespace MUSIC

} // namespace YSE

#endif // NOTE_H_INCLUDED
