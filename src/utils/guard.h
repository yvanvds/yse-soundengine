#pragma once
#include <vector>

// why?
// In the program interface, a lot of variables are used to change settings or values are read from them.
// The main thread (interface) runs about 30 times a second.
// The audio callback thread also needs these values and runs about 40 times a second and the processing time
// needed for that can be non-trivial.
// => Constantly locking the callback thread for every change and lookup slows it down too much.

// solution
// - A 'guard' contains 2 versions of the variable, simply by using guard<type> i instead of type i.
// - A global bool 'GuardState' decides what version of the var is used.
// - Guards register themselves to a global guard controller.
// 
// At the end of each program iteration, the callback is locked and the guard controller takes care of 
// copying the values down and enabling down state.

// After calculations, the guard controller copies the other way and enables up state again.

/*#define B 0
#define F 1

namespace YSE {

  // a base class is needed so that all guards can be included in a single vector (in guardControl)
  class baseGuard {
  public:
    virtual void up  () = 0;
    virtual void down() = 0;
  };


  // this controller is needed to copy values for all existing vars and toggle the state
  class guardControl {
  public:
    // used by global update function
    // down copies interface values to the real ones
    inline void down() { for (it = guards.begin(); it != guards.end(); ++it) (*it)->down(); }
    // up copies values back to interface
    inline void up  () { 
      int x = 0;
      for (it = guards.begin(); it != guards.end(); ++it) {
        x++;
        (*it)->up  (); 
      }
    } 

    // used by class guard
    inline void add(baseGuard * g) { guards.push_back(g); }
    inline void rem(baseGuard * g) { 
      for (it = guards.begin(); it != guards.end(); ++it) { 
        if(*it == g) { 
          *it = guards.back(); 
          guards.pop_back();
          return; 
        }
      }
      throw 1;
    } //trow exception if not found
  private:
    std::vector<baseGuard*> guards;
    std::vector<baseGuard*>::iterator it;
  };

  // global object wrapped in function so to be sure
  // it exists when needed
  guardControl & GuardControl();


  // below are all different types of guards

  // readGuards make internal values safely readable to the interface
  template <class T>
  class readGuard : baseGuard{
  public:
    inline T    b(   ) { return values[B]; }
    inline void b(T i) { values[B] = i; }

    inline T    f(   ) { return values[F]; }

    inline void init(T i) { values[F] = values[B] = i; }

    inline void up  () { values[F] = values[B]; }
    inline void down() {} // nothing happens here

    readGuard() { GuardControl().add(this); }
   ~readGuard() { GuardControl().rem(this); }

  private: 
    T values[2];
  };


  // writeGuards are for values that have to be changes by the interface, but are never read back
  template <class T>
  class writeGuard : baseGuard{
  public:
    inline T    b(   ) { return values[B]; }
    inline void b(T i) { values[B] = i; }

    inline void f(T i) { values[F] = i; }

    inline void init(T i) { values[F] = values[B] = i; }

    inline void up  () {}
    inline void down() { values[B] = values[F]; } 

    writeGuard() { GuardControl().add(this); }
   ~writeGuard() { GuardControl().rem(this); }

  private: 
    T values[2];
  };

  // use this for a two way guard
  template <class T>
  class rwGuard : baseGuard{
  public: 
    inline T    b(   ) { return values[B]; }
    inline void b(T i) { values[B] = i; }

    inline T    f(   ) { return values[F]; }
    inline void f(T i) { values[F] = i; }

    inline void init(T i) { values[F] = values[B] = i; }

    inline void up  () { values[F] = values[B]; }
    inline void down() { values[B] = values[F]; }


    rwGuard() { GuardControl().add(this); }
   ~rwGuard() { GuardControl().rem(this); }

  private:
    T values[2];
  };


  // readGuardsPtr makes existing internal values safely readable to the interface
  template <class T>
  class readGuardPtr : baseGuard {
  public:
    inline T f() { return interfaceValue; }

    inline void up  () { if (implementationValue) interfaceValue = *implementationValue; }
    inline void down() {} // nothing happens here

    inline void set(T & value) { implementationValue = &value; }
    readGuardPtr() { GuardControl().add(this); implementationValue = NULL; }
   ~readGuardPtr() { GuardControl().rem(this); }

  private: 
    T interfaceValue;
    T * implementationValue;
  };

  // only update back when value in front changed
  template <class T>
  class setGuard : baseGuard {
  public: 
    inline T    b(   ) { return values[B]; }
    inline void b(T i) { values[B] = i; }

    inline T    f(   ) { return values[F]; }
    inline void f(T i) { values[F] = i; changed = true;}

    inline void up  () { values[F] = values[B]; }
    inline void down() { if (changed) values[B] = values[F]; changed = false;}

    setGuard() { GuardControl().add(this); changed = false; }
   ~setGuard() { GuardControl().rem(this); }

  private:
    T values[2];
    Bool changed;
  };

  // setGuardsPtr makes existing internal values safely readable to the interface
  template <class T>
  class setGuardPtr : baseGuard {
  public:
    inline T f() { return interfaceValue; }
    inline void f(T i) { interfaceValue = i; changed = true; }

    inline void up  () { if (implementationValue) interfaceValue = *implementationValue; }
    inline void down() { if (changed && implementationValue) {
      *implementationValue = interfaceValue; 
      changed = false; 
    } }

    inline void set(T & value) { implementationValue = &value; }
    setGuardPtr() { GuardControl().add(this); implementationValue = NULL; changed = false; }
   ~setGuardPtr() { GuardControl().rem(this); }

  private:
    Bool changed;
    T interfaceValue;
    T * implementationValue;
  };



  // extendable guard for full control
  template <class T>
  class extendedGuard : baseGuard {
  public:
    inline T    f(   ) { return value; }
    inline void f(T i) { value = i; changed = true; }

    inline void up  () { if (             upFunc)   upFunc(value);                  }
    inline void down() { if (changed && downFunc) downFunc(value); changed = false; }

    inline void setFunction( (void * downfunc)(T & value), (void * upfunc)(T & value) ) { downFunc = downfunc; upFunc = upfunc; }
  private:
    void * downFunc(T & value);
    void * upFunc  (T & value);

    bool changed;
    T value;
  };

//} // namespace YSE

*/