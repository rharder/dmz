#include <dmzInputConsts.h>
#include <dmzInputModule.h>
#include "dmzQtModuleMapBasic.h"
#include <dmzRuntimePluginFactoryLinkSymbol.h>
#include <dmzQtUtil.h>
#include <dmzRuntimeConfig.h>
#include <dmzRuntimeConfigToTypesBase.h>
#include <dmzRuntimeConfigToNamedHandle.h>
#include <dmzRuntimeDefinitions.h>
#include <dmzRuntimePluginFactoryLinkSymbol.h>
#include <dmzRuntimePluginInfo.h>
#include <dmzRuntimeSession.h>
#include <qmapcontrol.h>
#include <QtGui/QtGui>


dmz::QtModuleMapBasic::QtModuleMapBasic (const PluginInfo &Info, Config &local) :
      QWidget (0),
      Plugin (Info),
      QtWidget (Info),
      QtModuleMap (Info),
      _log (Info),
      _inputModule (0),
      _inputModuleName (),
      _keyEvent (),
      _mouseEvent (),
      _map (0),
      _mapAdapter (0),
      _baseLayer (0) {

   _init (local);
}


dmz::QtModuleMapBasic::~QtModuleMapBasic () {

}


// Plugin Interface
void
dmz::QtModuleMapBasic::update_plugin_state (
      const PluginStateEnum State,
      const UInt32 Level) {

   if (State == PluginStateInit) {

   }
   else if (State == PluginStateStart) {

      _load_session ();
      setFocus (Qt::ActiveWindowFocusReason);
   }
   else if (State == PluginStateStop) {

      _save_session ();
   }
   else if (State == PluginStateShutdown) {

   }
}


void
dmz::QtModuleMapBasic::discover_plugin (
      const PluginDiscoverEnum Mode,
      const Plugin *PluginPtr) {

   if (Mode == PluginDiscoverAdd) {

      if (!_inputModule) {

         _inputModule = InputModule::cast (PluginPtr, _inputModuleName);
      }
   }
   else if (Mode == PluginDiscoverRemove) {

      if (_inputModule && (_inputModule == InputModule::cast (PluginPtr))) {

         _inputModule = 0;
      }
   }
}


// QtWidget Interface
QWidget *
dmz::QtModuleMapBasic::get_qt_widget () { return this; }


// Class Methods
void
dmz::QtModuleMapBasic::resizeEvent (QResizeEvent *event) {

   if (_map && event) {
      
      _map->resize (event->size () - QSize (1,1));
   }
   
   _handle_mouse_event (0, 0);
}


void
dmz::QtModuleMapBasic::keyPressEvent (QKeyEvent *event) {

   if (event) { _handle_key_event (*event, True); }
}


void
dmz::QtModuleMapBasic::keyReleaseEvent (QKeyEvent *event) {

   if (event) { _handle_key_event (*event, False); }
}


void
dmz::QtModuleMapBasic::mousePressEvent (QMouseEvent *event) {

   _handle_mouse_event (event, 0);
}


void
dmz::QtModuleMapBasic::mouseReleaseEvent (QMouseEvent *event) {

   _handle_mouse_event (event, 0);
}


void
dmz::QtModuleMapBasic::mouseMoveEvent (QMouseEvent *event) {

   _handle_mouse_event (event, 0);
}


void
dmz::QtModuleMapBasic::wheelEvent (QWheelEvent *event) {

   int numDegrees = event->delta() / 8;
   int numSteps = numDegrees / 15;

   if (numSteps > 0){
       for (int i = 0; i < numSteps; ++i)
           _map->zoomIn();
   } else {
       for (int i = 0; i < qAbs(numSteps); ++i)
           _map->zoomOut();
   }
   
//   evnt->accept();
   
   _handle_mouse_event (0, event);
}


void
dmz::QtModuleMapBasic::_handle_key_event (
      const QKeyEvent &Event,
      const Boolean KeyState) {

   if (!Event.isAutoRepeat ()) {

      UInt32 theKey (0);

      switch (Event.key ()) {

         case Qt::Key_F1: theKey = KeyF1; break;
         case Qt::Key_F2: theKey = KeyF2; break;
         case Qt::Key_F3: theKey = KeyF3; break;
         case Qt::Key_F4: theKey = KeyF4; break;
         case Qt::Key_F5: theKey = KeyF5; break;
         case Qt::Key_F6: theKey = KeyF6; break;
         case Qt::Key_F7: theKey = KeyF7; break;
         case Qt::Key_F8: theKey = KeyF8; break;
         case Qt::Key_F9: theKey = KeyF9; break;
         case Qt::Key_F10: theKey = KeyF10; break;
         case Qt::Key_F11: theKey = KeyF11; break;
         case Qt::Key_F12: theKey = KeyF12; break;
         case Qt::Key_Left: theKey = KeyLeftArrow; break;
         case Qt::Key_Up: theKey = KeyUpArrow; break;
         case Qt::Key_Right: theKey = KeyRightArrow; break;
         case Qt::Key_Down: theKey = KeyDownArrow; break;
         case Qt::Key_PageUp: theKey = KeyPageUp; break;
         case Qt::Key_PageDown: theKey = KeyPageDown; break;
         case Qt::Key_Home: theKey = KeyHome; break;
         case Qt::Key_End: theKey = KeyEnd; break;
         case Qt::Key_Insert: theKey = KeyInsert; break;
         case Qt::Key_Space: theKey = KeySpace; break;
         case Qt::Key_Escape: theKey = KeyEsc; break;
         case Qt::Key_Tab: theKey = KeyTab; break;
         case Qt::Key_Backspace: theKey = KeyBackspace; break;
         case Qt::Key_Enter: theKey = KeyEnter; break;
         case Qt::Key_Return: theKey = KeyEnter; break;
         case Qt::Key_Delete: theKey = KeyDelete; break;
         case Qt::Key_Shift : theKey = KeyShift; break;
         case Qt::Key_Control : theKey = KeyControl; break;
         case Qt::Key_Meta : theKey = KeyMeta; break;
         case Qt::Key_Alt : theKey = KeyAlt; break;

         default:

            if (!(Event.text ().isEmpty ())) {

               theKey = Event.text ().at (0).toAscii ();
            }
      }

      _keyEvent.set_key (theKey);
      _keyEvent.set_key_state (KeyState);

      if (_inputModule) { _inputModule->send_key_event (_keyEvent); }
   }
}


void
dmz::QtModuleMapBasic::_handle_mouse_event (QMouseEvent *me, QWheelEvent *we) {

   if (_inputModule && _map) {

      InputEventMouse event (_mouseEvent);

      QPoint pointOnCanvas (event.get_mouse_x (), event.get_mouse_y ());
      QPoint pointOnScreen (event.get_mouse_screen_x (), event.get_mouse_screen_y ());

      Qt::MouseButtons buttons;

      if (me) {

//         pointOnCanvas = _canvas->mapFrom (this, me->pos ());
         pointOnScreen = me->globalPos ();

         buttons = me->buttons ();
      }
      else if (we) {

//         pointOnCanvas = _canvas->mapFrom (this, we->pos ());
         pointOnScreen = we->globalPos ();

         buttons = we->buttons ();
      }

      event.set_mouse_position (pointOnCanvas.x (), pointOnCanvas.y ());
      event.set_mouse_screen_position (pointOnScreen.x (), pointOnScreen.y ());

      UInt32 mask (0);
      if (buttons & Qt::LeftButton) { mask |= 0x01 << 0; }
      if (buttons & Qt::RightButton) { mask |= 0x01 << 1; }
      if (buttons & Qt::MidButton) { mask |= 0x01 << 2; }

      event.set_button_mask (mask);

      Int32 deltaX (0), deltaY (0);

      if (we) {

         if (we->orientation () == Qt::Vertical) { deltaY = we->delta (); }
         else if (we->orientation () == Qt::Horizontal) { deltaX = we->delta (); }
      }

      event.set_scroll_delta (deltaX, deltaY);

      event.set_window_size (width (), height ());

      if (_mouseEvent.update (event)) {

         _inputModule->send_mouse_event (_mouseEvent);
      }
   }
}


void
dmz::QtModuleMapBasic::_save_session () {

}


void
dmz::QtModuleMapBasic::_load_session () {

}


void
dmz::QtModuleMapBasic::_init (Config &local) {

   //http://b.tile.cloudmade.com/b999cacc40c7586fbdd6407362411e4d/2/256/11/603/769.png
   
   qwidget_config_read ("widget", local, this);

   _map = new qmapcontrol::MapControl (QSize (200, 200));
   _map->setMouseTracking (true);
   _map->enablePersistentCache ();
//   _map->setMouseMode (qmapcontrol::MapControl::Dragging);
   
   // connect(
   //    _map, SIGNAL (viewChanged (const QPointF &, int)),
   //    this, SIGNAL (viewChanged (const QPointF &, int)));
   
   _mapAdapter = new qmapcontrol::OSMMapAdapter ();
   _baseLayer = new qmapcontrol::MapLayer ("base", _mapAdapter);
   
   _map->addLayer (_baseLayer);
   
   QVBoxLayout *layout (new QVBoxLayout ());
   layout->addWidget (_map);
   layout->setMargin (0);
   
   setLayout (layout);
   setMouseTracking (true);
   
   _inputModuleName = config_to_string ("module.input.name", local);

   _keyEvent.set_source_handle (get_plugin_handle ());
   _mouseEvent.set_source_handle (get_plugin_handle ());
}


extern "C" {

DMZ_PLUGIN_FACTORY_LINK_SYMBOL dmz::Plugin *
create_dmzQtModuleMapBasic (
      const dmz::PluginInfo &Info,
      dmz::Config &local,
      dmz::Config &global) {

   return new dmz::QtModuleMapBasic (Info, local);
}

};
