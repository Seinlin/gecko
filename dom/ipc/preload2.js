(function (global) {
  "use strict";

  let Cu = Components.utils;
  let Cc = Components.classes;
  let Ci = Components.interfaces;

  Cu.import("resource://gre/modules/IndexedDBHelper.jsm");
  Cu.import("resource://gre/modules/SettingsDB.jsm");
  Cc["@mozilla.org/cookieService;1"].getService(Ci["nsICookieService"]);
  Cc["@mozilla.org/preferences-service;1"].getService(Ci["nsIPrefBranch"]);
  docShell.createAboutBlankContentViewer(null);
})(this);
