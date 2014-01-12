/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

let { classes: Cc, interfaces: Ci, results: Cr, utils: Cu }  = Components;
Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/Geometry.jsm");
Cu.import("resource://gre/modules/BrowserElementPromptService.jsm");

function debug(msg) {
  //dump("BrowserElementChild - " + msg + "\n");
}

// NB: this must happen before we process any messages from
// mozbrowser API clients.
docShell.isActive = true;

let infos = sendSyncMessage('browser-element-api:call',
                            { 'msg_name': 'hello' })[0];
docShell.QueryInterface(Ci.nsIDocShellTreeItem).name = infos.name;
docShell.setFullscreenAllowed(infos.fullscreenAllowed);


function parentDocShell(docshell) {
  if (!docshell) {
    return null;
  }
  let treeitem = docshell.QueryInterface(Ci.nsIDocShellTreeItem);
  return treeitem.parent ? treeitem.parent.QueryInterface(Ci.nsIDocShell) : null;
}

function isTopBrowserElement(docShell) {
  while (docShell) {
    docShell = parentDocShell(docShell);
    if (docShell && docShell.isBrowserOrApp) {
      return false;
    }
  }
  return true;
}

if (!('BrowserElementIsPreloaded' in this)) {
  if (isTopBrowserElement(docShell) &&
      Services.prefs.getBoolPref("dom.mozInputMethod.enabled")) {
    try {
      Services.scriptloader.loadSubScript("chrome://global/content/forms.js");
    } catch (e) {
    }
  }
  // Those are produc-specific files that's sometimes unavailable.
  try {
    Services.scriptloader.loadSubScript("chrome://browser/content/ErrorPage.js");
  } catch (e) {
  }

  Services.scriptloader.loadSubScript("chrome://global/content/BrowserElementPanning.js");
  ContentPanning.init();

  Services.scriptloader.loadSubScript("chrome://global/content/BrowserElementChildPreload.js");
} else {
  ContentPanning.init();
}

BrowserElementPromptService.mapWindowToBrowserElementChild(content, this);

// This is necessary to get security web progress notifications.
var securityUI = Cc['@mozilla.org/secure_browser_ui;1']
      .createInstance(Ci.nsISecureBrowserUI);
securityUI.init(content);

var BrowserElementIsReady = true;
