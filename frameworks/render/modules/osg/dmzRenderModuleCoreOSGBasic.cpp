#include "dmzRenderModuleCoreOSGBasic.h"
#include <dmzObjectConsts.h>
#include <dmzObjectModule.h>
#include <dmzObjectAttributeMasks.h>
#include <dmzRenderConsts.h>
#include <dmzRenderObjectDataOSG.h>
#include <dmzRenderUtilOSG.h>
#include <dmzRuntimeConfig.h>
#include <dmzRuntimeConfigToNamedHandle.h>
#include <dmzRuntimeConfigToTypesBase.h>
#include <dmzRuntimeConfigToStringContainer.h>
#include <dmzRuntimePluginFactoryLinkSymbol.h>
#include <dmzRuntimePluginInfo.h>
#include <dmzRuntimeLoadPlugins.h>
#include <dmzTypesUUID.h>
#include <dmzSystem.h>
#include <dmzSystemFile.h>
#include <osg/DeleteHandler>
#include <osg/LightSource>
#include <osg/Referenced>
#include <osg/Version>
#include <osgDB/Registry>
#include <osgUtil/Optimizer>


dmz::RenderModuleCoreOSGBasic::RenderModuleCoreOSGBasic (
      const PluginInfo &Info,
      Config &local,
      Config &global) :
      Plugin (Info),
      TimeSlice (Info),
      ObjectObserverUtil (Info, local),
      RenderModuleCoreOSG (Info),
      _log (Info),
      _defs (Info),
      _extensions (Info.get_context (), &_log),
      _cullMask (0x001),
      _isectMask (0),
      _defaultHandle (0),
      _bvrHandle (0),
      _dirtyObjects (0) {

   _log.info << "Built using Open Scene Graph v"
      << Int32 (OPENSCENEGRAPH_MAJOR_VERSION) << "."
      << Int32 (OPENSCENEGRAPH_MINOR_VERSION) << "."
      << Int32 (OPENSCENEGRAPH_PATCH_VERSION) << endl;

   const UInt32 StaticMask (0x01 << 1);
   const UInt32 EntityMask (0x01 << 2);
   const UInt32 GlyphMask (0x01 << 3);
   const UInt32 OverlayMask (0x01 << 4);

   _isectMaskTable.store (
      _defs.create_named_handle (RenderIsectStaticName),
      new UInt32 (StaticMask));

   _isectMaskTable.store (
      _defs.create_named_handle (RenderIsectEntityName),
      new UInt32 (EntityMask));

   _isectMaskTable.store (
      _defs.create_named_handle (RenderIsectGlyphName),
      new UInt32 (GlyphMask));

   _isectMaskTable.store (
      _defs.create_named_handle (RenderIsectOverlayName),
      new UInt32 (OverlayMask));

   _scene = new osg::Group;

   _overlay = new osg::Group;
   _scene->addChild (_overlay.get ());

   _isect = new osg::Group;
   _scene->addChild (_isect.get ());

   _staticObjects = new osg::Group;
   _isect->addChild (_staticObjects.get ());

   _dynamicObjects = new osg::Group;
   _dynamicObjects->setDataVariance (osg::Object::DYNAMIC);
   _isect->addChild (_dynamicObjects.get ());

   _init (local, global);

   HashTableHandleIterator it;
   UInt32 *mask (0);

   while (_isectMaskTable.get_next (it, mask)) { _isectMask |= *mask; }

   _staticObjects->setNodeMask (
      (_staticObjects->getNodeMask () & ~_isectMask) | StaticMask);

   _dynamicObjects->setNodeMask (
      (_dynamicObjects->getNodeMask () & ~_isectMask) | EntityMask | GlyphMask);

   _overlay->setNodeMask (OverlayMask | _cullMask);
}


dmz::RenderModuleCoreOSGBasic::~RenderModuleCoreOSGBasic () {

   _extensions.remove_plugins ();
   _extensions.delete_plugins ();
   _objectTable.empty ();
   _viewTable.empty ();

   osg::DeleteHandler *dh (osg::Referenced::getDeleteHandler ());

   _scene = 0;

   if (dh) { dh->flush (); }
}


// Plugin Interface
void
dmz::RenderModuleCoreOSGBasic::update_plugin_state (
      const PluginStateEnum State,
      const UInt32 Level) {

   if (State == PluginStateInit) {

      if (_staticObjects.valid ()) {

         osgUtil::Optimizer optimizer;
         optimizer.optimize(_staticObjects.get());
      }
      
      _extensions.init_plugins ();
   }
   else if (State == PluginStateStart) {

      _extensions.start_plugins ();
   }
   else if (State == PluginStateStop) {

      _extensions.stop_plugins ();
   }
   else if (State == PluginStateShutdown) {

      _extensions.shutdown_plugins ();
   }
}


void
dmz::RenderModuleCoreOSGBasic::discover_plugin (
      const PluginDiscoverEnum Mode,
      const Plugin *PluginPtr) {

   if (Mode == PluginDiscoverAdd) {

      _extensions.discover_external_plugin (PluginPtr);
   }
   else if (Mode == PluginDiscoverRemove) {

      _extensions.remove_external_plugin (PluginPtr);
   }
}


// Time Slice Interface
void
dmz::RenderModuleCoreOSGBasic::update_time_slice (const Float64 DeltaTime) {

   ObjectModule *objMod (get_object_module ());

   while (_dirtyObjects) {

      ObjectStruct *os (_dirtyObjects);
      _dirtyObjects = os->next;

      os->transform->setMatrix (to_osg_matrix (os->ori, os->pos, os->scale));

      if (objMod) {

         const osg::BoundingSphere &Bvs = os->transform->getBound ();
         const Float64 Radius = Bvs.radius ();
         objMod->store_scalar (os->Object, _bvrHandle, Radius);
      }

      if (os->destroyed) { 

         if (_dynamicObjects.valid ()) {

            _dynamicObjects->removeChild (os->transform.get ());
         }

         delete os; os = 0;
      }
      else { os->next = 0; os->dirty = False; }
   }
}


// Object Observer Interface
void
dmz::RenderModuleCoreOSGBasic::destroy_object (
      const UUID &Identity,
      const Handle ObjectHandle) {

   ObjectStruct *os (_objectTable.remove (ObjectHandle));

   if (os) {

      if (os->dirty) { os->destroyed = True; }
      else {

         if (_dynamicObjects.valid ()) {

            _dynamicObjects->removeChild (os->transform.get ());
         }

         delete os; os = 0;
      }
   }
}


void
dmz::RenderModuleCoreOSGBasic::update_object_position (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Vector &Value,
      const Vector *PreviousValue) {

   ObjectStruct *os (_objectTable.lookup (ObjectHandle));

   if (os) {

      os->pos = Value;

      if (!os->dirty) {

         os->dirty = True;
         os->next = _dirtyObjects;
         _dirtyObjects = os;
      }
   }
}


void
dmz::RenderModuleCoreOSGBasic::update_object_scale (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Vector &Value,
      const Vector *PreviousValue) {

   ObjectStruct *os (_objectTable.lookup (ObjectHandle));

   if (os) {

      os->scale = Value;

      if (!os->dirty) {

         os->dirty = True;
         os->next = _dirtyObjects;
         _dirtyObjects = os;
      }
   }
}


void
dmz::RenderModuleCoreOSGBasic::update_object_orientation (
      const UUID &Identity,
      const Handle ObjectHandle,
      const Handle AttributeHandle,
      const Matrix &Value,
      const Matrix *PreviousValue) {

   ObjectStruct *os (_objectTable.lookup (ObjectHandle));

   if (os) {

      os->ori = Value;

      if (!os->dirty) {

         os->dirty = True;
         os->next = _dirtyObjects;
         _dirtyObjects = os;
      }
   }
}

// RenderModuleCoreOSG Interface
dmz::UInt32
dmz::RenderModuleCoreOSGBasic::get_cull_mask () { return _cullMask; }


dmz::UInt32
dmz::RenderModuleCoreOSGBasic::get_master_isect_mask () { return _isectMask; }


dmz::UInt32
dmz::RenderModuleCoreOSGBasic::lookup_isect_mask (const String &AttributeName) {

   UInt32 *ptr = _isectMaskTable.lookup (_defs.lookup_named_handle (AttributeName));

   return ptr ? *ptr : 0;
}


dmz::UInt32
dmz::RenderModuleCoreOSGBasic::lookup_isect_mask (const Handle Attribute) {

   UInt32 *ptr = _isectMaskTable.lookup (Attribute);

   return ptr ? *ptr : 0;
}


osg::Group *
dmz::RenderModuleCoreOSGBasic::get_scene () { return _scene.get (); }


osg::Group *
dmz::RenderModuleCoreOSGBasic::get_overlay () { return _overlay.get (); }


osg::Group *
dmz::RenderModuleCoreOSGBasic::get_isect () { return _isect.get (); }

osg::Group *
dmz::RenderModuleCoreOSGBasic::get_static_objects () { return _staticObjects.get (); }


osg::Group *
dmz::RenderModuleCoreOSGBasic::get_dynamic_objects () { return _dynamicObjects.get (); }


osg::Group *
dmz::RenderModuleCoreOSGBasic::create_dynamic_object (const Handle ObjectHandle) {

   osg::Group *result (0);

   ObjectStruct *os (_objectTable.lookup (ObjectHandle));

   if (!os) {

      os = new ObjectStruct (ObjectHandle);

      if (os && !_objectTable.store (ObjectHandle, os)) { delete os; os = 0; }

      if (os) {

         os->transform->setUserData (new RenderObjectDataOSG (ObjectHandle));
         os->transform->setDataVariance (osg::Object::DYNAMIC);

         ObjectModule *objMod (get_object_module ());

         if (objMod) {

            objMod->lookup_position (ObjectHandle, _defaultHandle, os->pos);
            objMod->lookup_scale (ObjectHandle, _defaultHandle, os->scale);
            objMod->lookup_orientation (ObjectHandle, _defaultHandle, os->ori);
            os->transform->setMatrix (to_osg_matrix (os->ori, os->pos, os->scale));
         }

         os->dirty = True;
         os->next = _dirtyObjects;
         _dirtyObjects = os;

         if (_dynamicObjects.valid ()) {

            _dynamicObjects->addChild (os->transform.get ());
         }
      }
   }

   if (os) { result = os->transform.get (); }

   return result;
}


dmz::Boolean
dmz::RenderModuleCoreOSGBasic::destroy_dynamic_object (const Handle ObjectHandle) {

   static const UUID Empty;

   Boolean result = (_objectTable.lookup (ObjectHandle) ? True : False);

   destroy_object (Empty, ObjectHandle);

   return result;
}


osg::Group *
dmz::RenderModuleCoreOSGBasic::lookup_dynamic_object (const Handle ObjectHandle) {

   osg::Group *result (0);

   ObjectStruct *os (_objectTable.lookup (ObjectHandle));

   if (os) { result = os->transform.get (); }

   return result;
}


dmz::Boolean
dmz::RenderModuleCoreOSGBasic::add_view (
      const String &ViewName,
      osgViewer::View *view) {

   Boolean result (False);

   ViewStruct *vs = new ViewStruct (ViewName, view);

   if (vs && _viewTable.store (ViewName, vs)) {

      result = True;

      if (view) {

         osg::Camera *camera = view->getCamera ();

         if (camera) { camera->setCullMask (_cullMask); }
      }
   }
   else if (vs) { delete vs; vs = 0; }

   return result;
}


osgViewer::View *
dmz::RenderModuleCoreOSGBasic::lookup_view (const String &ViewName) {

   ViewStruct *vs (_viewTable.lookup (ViewName));

   return vs ? vs->view.get () : 0;
}


osgViewer::View *
dmz::RenderModuleCoreOSGBasic::remove_view (const String &ViewName) {

   osgViewer::View *result (0);
   ViewStruct *vs (_viewTable.remove (ViewName));

   if (vs) {

      result = vs->view.get ();
      delete vs; vs = 0;
   }

   return result;
}


void
dmz::RenderModuleCoreOSGBasic::_init (Config &local, Config &global) {

   const String UpStr = config_to_string ("osg-up.value", local, "y").to_lower ();
   if (UpStr == "y") { set_osg_y_up (); _log.info << "OSG render Y is up." << endl; }
   else if (UpStr == "z") { set_osg_z_up (); _log.info << "OSG render Z is up" << endl; }
   else {

      _log.warn << "Unknown osg up type: " << UpStr << ". Defaulting to Y up." << endl;
   }

   Config pluginList;

   if (local.lookup_all_config ("plugin-list.plugin", pluginList)) {

      RuntimeContext *context (get_plugin_runtime_context ());

      if (dmz::load_plugins (context, pluginList, local, global, _extensions, &_log)) {

         _extensions.discover_plugins ();
         _extensions.discover_external_plugin (this);
      }
   }

   osgDB::Registry *reg = osgDB::Registry::instance ();
   Config pathList;

   if (reg && local.lookup_all_config ("loader.path", pathList)) {

      osgDB::FilePathList &fpl = reg->getLibraryFilePathList ();

      ConfigIterator it;
      Config path;

      while (pathList.get_next_config (it, path)) {

         String pathStr = config_to_string ("value", path);

         if (get_absolute_path (pathStr, pathStr)) {

            fpl.push_back (pathStr.get_buffer ());
         }
      }

   }

   if(reg) {

      reg->setBuildKdTreesHint(osgDB::ReaderWriter::Options::BUILD_KDTREES);
   }

   _defaultHandle = activate_default_object_attribute (
      ObjectDestroyMask | ObjectPositionMask | ObjectScaleMask | ObjectOrientationMask);

   _bvrHandle = config_to_named_handle (
      "bounding-volume-radius-attribute.name",
      local,
      ObjectAttributeBoundingVolumeRaidusName,
      get_plugin_runtime_context ());
}


extern "C" {

DMZ_PLUGIN_FACTORY_LINK_SYMBOL dmz::Plugin *
create_dmzRenderModuleCoreOSGBasic (
      const dmz::PluginInfo &Info,
      dmz::Config &local,
      dmz::Config &global) {

   return new dmz::RenderModuleCoreOSGBasic (Info, local, global);
}

};
