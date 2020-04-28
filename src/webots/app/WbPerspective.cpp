// Copyright 1996-2020 Cyberbotics Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "WbPerspective.hpp"

#include "WbApplicationInfo.hpp"
#include "WbLog.hpp"
#include "WbProject.hpp"
#include "WbSolid.hpp"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTextStream>

#include <cassert>

#ifdef _WIN32
#include <windows.h>
#endif

WbPerspective::WbPerspective(const QString &worldPath) :
  mMaximizedDockId(-1),
  mCentralWidgetVisible(true),
  mSelectedTab(-1),
  mOrthographicViewHeight(1.0),
  mSelectionDisabled(false),
  mViewpointLocked(false) {
  const QFileInfo info(worldPath);
  mBaseName = info.absolutePath() + "/." + info.completeBaseName();
  mVersion = WbApplicationInfo::version();
}

WbPerspective::~WbPerspective() {
  clearRenderingDevicesPerspectiveList();
}

bool WbPerspective::readContent(QTextStream &in, bool reloading) {
  // only version later than v6 are currently supported
  if (mVersion.majorNumber() < 6)
    return false;

  const bool skipNodeIdsOptions = mVersion.majorNumber() < 2018;
  while (!in.atEnd()) {
    QString line(in.readLine());
    QTextStream ls(&line, QIODevice::ReadOnly);
    QString key;
    ls >> key;
    // ignore perspective for version 6 (wxWidgets): we are not compatible
    if (key == "perspectives:" && mVersion.majorNumber() > 6) {
      QByteArray hex;
      ls >> hex;
      mState = QByteArray::fromHex(hex);
    } else if (key == "simulationViewPerspectives:") {
      QByteArray hex;
      ls >> hex;
      mSimulationViewState = QByteArray::fromHex(hex);
    } else if (key == "sceneTreePerspectives:") {
      QByteArray hex;
      ls >> hex;
      mSceneTreeState = QByteArray::fromHex(hex);
    } else if (key == "minimizedPerspectives:") {
      QByteArray hex;
      ls >> hex;
      mMinimizedState = QByteArray::fromHex(hex);
    } else if (key == "maximizedDockId:")
      ls >> mMaximizedDockId;
    else if (key == "centralWidgetVisible:") {
      int i;
      ls >> i;
      mCentralWidgetVisible = i;
    } else if (key == "projectionMode:") {
      if (reloading)
        continue;
      ls >> mProjectionMode;
    } else if (key == "renderingMode:") {
      if (reloading)
        continue;
      ls >> mRenderingMode;
    } else if (key == "selectionDisabled:") {
      if (reloading)
        continue;
      int i;
      ls >> i;
      mSelectionDisabled = i;
    } else if (key == "viewpointLocked:") {
      if (reloading)
        continue;
      int i;
      ls >> i;
      mViewpointLocked = i;
    } else if (key == "orthographicViewHeight:") {
      double value;
      ls >> value;
      setOrthographicViewHeight(value);
    } else if (key == "textFiles:") {
      ls >> mSelectedTab;
      mFilesList.clear();
      const QDir dir(WbProject::current()->dir());
      const QRegExp rx("(\"[^\"]*\")");  // to match string literals
      int pos = 0;
      while ((pos = rx.indexIn(line, pos)) != -1) {
        QString file(rx.cap(1));
        file.remove("\"");                              // remove double quotes
        mFilesList.append(dir.absoluteFilePath(file));  // make absolute path
        pos += rx.matchedLength();
      }
    } else if (key == "documentationBook:")
      ls >> mDocumentationBook;
    else if (key == "documentationPage:")
      ls >> mDocumentationPage;
    else if (key == "robotWindow:") {
      if (!mRobotWindowNodeNames.isEmpty() || skipNodeIdsOptions)
        continue;
      QString s = line.right(line.length() - 12).trimmed();  // remove label
      splitUniqueNameList(s, mRobotWindowNodeNames);
    } else if (key == "globalOptionalRendering:") {
      if (!mEnabledOptionalRenderingList.isEmpty() || reloading)
        continue;
      const QString s = line.right(line.length() - 24).trimmed();  // remove label
      splitUniqueNameList(s, mEnabledOptionalRenderingList);
    } else if (key == "centerOfMass:") {
      if (!mCenterOfMassNodeNames.isEmpty() || skipNodeIdsOptions)
        continue;
      QString s = line.right(line.length() - 13).trimmed();  // remove label
      splitUniqueNameList(s, mCenterOfMassNodeNames);
    } else if (key == "centerOfBuoyancy:") {
      if (!mCenterOfBuoyancyNodeNames.isEmpty() || skipNodeIdsOptions)
        continue;
      QString s = line.right(line.length() - 17).trimmed();  // remove label
      splitUniqueNameList(s, mCenterOfBuoyancyNodeNames);
    } else if (key == "supportPolygon:") {
      if (!mSupportPolygonNodeNames.isEmpty() || skipNodeIdsOptions)
        continue;
      QString s = line.right(line.length() - 15).trimmed();  // remove label
      splitUniqueNameList(s, mSupportPolygonNodeNames);
    } else if (key == "consoles:") {
      const QString s = line.right(line.length() - 10).trimmed();  // remove label
      QStringList values = s.split(";");
      mConsoles.append(values);
    } else if (key == "renderingDevicePerspectives:") {
      if (skipNodeIdsOptions)
        continue;

      QString s = line.right(line.length() - 29).trimmed();  // remove label
      QStringList values = s.split(";");
      int count = values.size();
      if (count < 5)
        // invalid
        continue;

      QString deviceUniqueName = values.takeFirst();
      while (values.size() > 9)
        // handle case where a Solid name contains the character ';'
        deviceUniqueName += ";" + values.takeFirst();
      mRenderingDevicesPerspectiveList.insert(deviceUniqueName, values);
    } else if (key.startsWith("x3dExport-")) {
      QString label = key.split("-")[1].remove(":");
      QString value;
      ls >> value;
      mX3dExportParameters.insert(label, value);
    } else
      WbLog::warning(QObject::tr("Unknown key in perspective file: %1 (ignored).").arg(key));
  }

  return true;
}

bool WbPerspective::load(bool reloading) {
  // reset version
  mVersion = WbApplicationInfo::version();

  mRobotWindowNodeNames.clear();
  if (!reloading)
    mEnabledOptionalRenderingList.clear();
  mConsoles.clear();
  clearRenderingDevicesPerspectiveList();
  clearEnabledOptionalRenderings();

  QFile file(mBaseName + ".wbproj");
  if (!file.open(QIODevice::ReadOnly))
    return false;

  QTextStream in(&file);
  if (in.atEnd())
    return false;

  const QString header(in.readLine());

  bool found = mVersion.fromString(header, "^Webots Project File version ", "$");
  if (!found || mVersion > WbApplicationInfo::version())
    // don't support forward compatibility
    return false;

  bool success = readContent(in, reloading);

  // make sure we explicitly close our input file
  file.close();

  return success;
}

bool WbPerspective::save() const {
  const QString fileName(mBaseName + ".wbproj");
  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly) || !qgetenv("WEBOTS_DISABLE_SAVE_PERSPECTIVE_ON_CLOSE").isEmpty())
    return false;

  QTextStream out(&file);
  out << "Webots Project File version " << WbApplicationInfo::version().toString(false) << "\n";
  assert(!mState.isEmpty());
  out << "perspectives: " << mState.toHex() << "\n";
  assert(!mSimulationViewState.isEmpty());
  out << "simulationViewPerspectives: " << mSimulationViewState.toHex() << "\n";
  assert(!mSceneTreeState.isEmpty());
  out << "sceneTreePerspectives: " << mSceneTreeState.toHex() << "\n";
  if (!mMinimizedState.isEmpty())
    out << "minimizedPerspectives: " << mMinimizedState.toHex() << "\n";
  out << "maximizedDockId: " << mMaximizedDockId << "\n";
  out << "centralWidgetVisible: " << (int)mCentralWidgetVisible << "\n";
  if (!mProjectionMode.isEmpty())
    out << "projectionMode: " << mProjectionMode << "\n";
  if (!mRenderingMode.isEmpty())
    out << "renderingMode: " << mRenderingMode << "\n";
  out << "selectionDisabled: " << (int)mSelectionDisabled << "\n";
  out << "viewpointLocked: " << (int)mViewpointLocked << "\n";
  out << "orthographicViewHeight: " << (double)mOrthographicViewHeight << "\n";
  out << "textFiles: " << mSelectedTab;
  // convert to relative paths and save
  const QDir dir(WbProject::current()->dir());
  foreach (const QString file, mFilesList)
    out << " \"" << dir.relativeFilePath(file) << "\"";
  out << "\n";
  if (!mDocumentationBook.isEmpty())
    out << "documentationBook: " << mDocumentationBook << "\n";
  if (!mDocumentationPage.isEmpty())
    out << "documentationPage: " << mDocumentationPage << "\n";
  if (!mRobotWindowNodeNames.isEmpty())
    out << "robotWindow: " << joinUniqueNameList(mRobotWindowNodeNames) << "\n";
  if (!mEnabledOptionalRenderingList.isEmpty())
    out << "globalOptionalRendering: " << joinUniqueNameList(mEnabledOptionalRenderingList) << "\n";
  if (!mCenterOfMassNodeNames.isEmpty())
    out << "centerOfMass: " << joinUniqueNameList(mCenterOfMassNodeNames) << "\n";
  if (!mCenterOfBuoyancyNodeNames.isEmpty())
    out << "centerOfBuoyancy: " << joinUniqueNameList(mCenterOfBuoyancyNodeNames) << "\n";
  if (!mSupportPolygonNodeNames.isEmpty())
    out << "supportPolygon: " << joinUniqueNameList(mSupportPolygonNodeNames) << "\n";

  for (int i = 0; i < mConsoles.size(); ++i)
    out << "consoles: " << mConsoles.at(i).join(";") << "\n";

  QHash<QString, QStringList>::const_iterator it;
  for (it = mRenderingDevicesPerspectiveList.constBegin(); it != mRenderingDevicesPerspectiveList.constEnd(); ++it)
    out << "renderingDevicePerspectives: " << it.key() << ";" << it.value().join(";") << "\n";

  QStringList x3dParametersKeys(mX3dExportParameters.keys());
  x3dParametersKeys.sort();
  foreach (QString key, x3dParametersKeys)
    out << "x3dExport-" << key << ": " << mX3dExportParameters.value(key) << "\n";

  file.close();

#ifdef _WIN32
  // set hidden attribute to WBPROJ file
  LPCSTR nativePath = QDir::toNativeSeparators(fileName).toUtf8().constData();
  SetFileAttributes(nativePath, GetFileAttributes(nativePath) | FILE_ATTRIBUTE_HIDDEN);
#endif

  return true;
}

void WbPerspective::setSimulationViewState(QList<QByteArray> state) {
  assert(state.size() == 2);
  mSimulationViewState = state[0];
  mSceneTreeState = state[1];
}

QList<QByteArray> WbPerspective::simulationViewState() const {
  QList<QByteArray> state;
  state << mSimulationViewState << mSceneTreeState;
  return state;
}

void WbPerspective::enableGlobalOptionalRendering(const QString &optionalRenderingName, bool enable) {
  if (!enable)
    mEnabledOptionalRenderingList.removeAll(optionalRenderingName);
  else if (!mEnabledOptionalRenderingList.contains(optionalRenderingName))
    mEnabledOptionalRenderingList.append(optionalRenderingName);
}

void WbPerspective::clearEnabledOptionalRenderings() {
  mCenterOfMassNodeNames.clear();
  mCenterOfBuoyancyNodeNames.clear();
  mSupportPolygonNodeNames.clear();
}

void WbPerspective::setRenderingDevicePerspective(const QString &deviceUniqueName, const QStringList &perspective) {
  QStringList value(perspective);
  if (deviceUniqueName.contains(";") && value.size() < 9) {
    assert(value.size() == 4);
    // in order to correctly retrieve the device unique name at load we have to add the external window properties
    value << "0"
          << "0"
          << "0"
          << "0"
          << "0";
  }
  mRenderingDevicesPerspectiveList.insert(deviceUniqueName, value);
}

QStringList WbPerspective::renderingDevicePerspective(const QString &deviceUniqueName) const {
  return mRenderingDevicesPerspectiveList.value(deviceUniqueName);
}

void WbPerspective::clearRenderingDevicesPerspectiveList() {
  mRenderingDevicesPerspectiveList.clear();
}

void WbPerspective::setX3dExportParameter(const QString &key, QString value) {
  mX3dExportParameters.insert(key, value);
}

QString WbPerspective::joinUniqueNameList(const QStringList &nameList) {
  return nameList.join("::");
}

void WbPerspective::splitUniqueNameList(const QString &text, QStringList &targetList) {
  targetList.clear();
  if (text.isEmpty())
    return;
  // extract solid unique names joined by '::'
  targetList = WbSolid::splitUniqueNamesByEscapedPattern(text, "::");
}
