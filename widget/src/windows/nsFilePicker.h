/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 * 
 * The Original Code is the Mozilla browser.
 * 
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation. Portions created by Netscape are
 * Copyright (C) 2000 Netscape Communications Corporation. All
 * Rights Reserved.
 * 
 * Contributor(s): 
 *   Stuart Parmenter <pavlov@netscape.com>
 */

#ifndef nsFilePicker_h__
#define nsFilePicker_h__

#define NS_IMPL_IDS
#include "nsICharsetConverterManager.h"
#undef NS_IMPL_IDS
#include "nsBaseFilePicker.h"
#include "nsString.h"


/**
 * Native Windows FileSelector wrapper
 */

class nsFilePicker : public nsBaseFilePicker
{
public:
  nsFilePicker(); 
  virtual ~nsFilePicker();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIFILEPICKER

protected:
  /* method from nsBaseFilePicker */
  NS_IMETHOD CreateNative(nsIWidget *aParent,
                          const PRUnichar *aTitle,
                          PRInt16 aMode);



  static void GetFileSystemCharset(nsString & fileSystemCharset);
  char * ConvertToFileSystemCharset(const PRUnichar *inString);
  PRUnichar * ConvertFromFileSystemCharset(const char *inString);

  HWND                   mWnd;
  nsString               mTitle;
  nsFileDlgMode          mMode;
  nsString               mFile;
  PRUint32               mNumberOfFilters;  
  const nsString*        mTitles;
  const nsString*        mFilters;
  nsString               mDefault;
  nsIUnicodeEncoder*     mUnicodeEncoder;
  nsIUnicodeDecoder*     mUnicodeDecoder;
  nsCOMPtr<nsIFile>      mDisplayDirectory;
};

#endif // nsFilePicker_h__
