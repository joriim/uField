/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef UFIELD_UFIELDAPP_H
#define UFIELD_UFIELDAPP_H

#include <memory>

class ActiveProject;
class LocalProjectsManager;
class QQmlContext;

namespace ufield
{
  class DriveSync;
  class ToolDispatcher;

  /**
   * μField's objects in the app, created once in main.cpp (the only place that knows
   * upstream's ActiveProject and LocalProjectsManager): the ToolDispatcher with its tool
   * handlers, and Google Drive sync. Compiled into the app target, unlike the ufield/
   * libraries, so it may use upstream's core and app types.
   */
  class UfieldApp
  {
    public:
      UfieldApp( LocalProjectsManager &localProjects, ActiveProject &activeProject );
      ~UfieldApp();

      //! QML context properties: __ufieldTools (ToolDispatcher) and __ufieldDriveSync (state for the Drive tab).
      void registerContextProperties( QQmlContext *context );

      ToolDispatcher &tools() { return *mTools; }

    private:
      std::unique_ptr<DriveSync> mDrive;
      std::unique_ptr<ToolDispatcher> mTools;
  };

} // namespace ufield

#endif // UFIELD_UFIELDAPP_H
