
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_swing_plaf_basic_BasicInternalFrameUI$ComponentHandler__
#define __javax_swing_plaf_basic_BasicInternalFrameUI$ComponentHandler__

#pragma interface

#include <java/lang/Object.h>
extern "Java"
{
  namespace java
  {
    namespace awt
    {
      namespace event
      {
          class ComponentEvent;
      }
    }
  }
  namespace javax
  {
    namespace swing
    {
      namespace plaf
      {
        namespace basic
        {
            class BasicInternalFrameUI;
            class BasicInternalFrameUI$ComponentHandler;
        }
      }
    }
  }
}

class javax::swing::plaf::basic::BasicInternalFrameUI$ComponentHandler : public ::java::lang::Object
{

public: // actually protected
  BasicInternalFrameUI$ComponentHandler(::javax::swing::plaf::basic::BasicInternalFrameUI *);
public:
  virtual void componentHidden(::java::awt::event::ComponentEvent *);
  virtual void componentMoved(::java::awt::event::ComponentEvent *);
  virtual void componentResized(::java::awt::event::ComponentEvent *);
  virtual void componentShown(::java::awt::event::ComponentEvent *);
public: // actually package-private
  ::javax::swing::plaf::basic::BasicInternalFrameUI * __attribute__((aligned(__alignof__( ::java::lang::Object)))) this$0;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_swing_plaf_basic_BasicInternalFrameUI$ComponentHandler__
