/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is Netscape Communications
 * Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 */

#include "nsCOMPtr.h"
#include "nsTreeFrame.h"
#include "nsIStyleContext.h"
#include "nsIContent.h"
#include "nsCSSRendering.h"
#include "nsTreeCellFrame.h"
#include "nsTableColFrame.h"
#include "nsTreeRowFrame.h"
#include "nsCellMap.h"
#include "nsIDOMXULTreeElement.h"
#include "nsINameSpaceManager.h"
#include "nsTreeRowGroupFrame.h"
#include "nsXULAtoms.h"
#include "nsHTMLAtoms.h"
#include "nsIDOMNodeList.h"
#include "nsIDOMXULTreeElement.h"
#include "nsTreeTwistyListener.h"
#include "nsIPresContext.h"
#include "nsIPresShell.h"
#include "nsIReflowCommand.h"
#include "nsHTMLParts.h"

#include "nsIDOMRange.h"
#include "nsIComponentManager.h"

#include "nsLayoutCID.h"
static NS_DEFINE_CID(kCRangeCID, NS_RANGE_CID);
#include "nsIContentIterator.h"
static NS_DEFINE_IID(kCContentIteratorCID, NS_CONTENTITERATOR_CID);
#include "nsLayoutAtoms.h"

//
// NS_NewTreeFrame
//
// Creates a new tree frame
//
nsresult
NS_NewTreeFrame (nsIPresShell* aPresShell, nsIFrame** aNewFrame)
{
  NS_PRECONDITION(aNewFrame, "null OUT ptr");
  if (nsnull == aNewFrame) {
    return NS_ERROR_NULL_POINTER;
  }
  nsTreeFrame* it = new (aPresShell) nsTreeFrame;
  if (!it)
    return NS_ERROR_OUT_OF_MEMORY;

  *aNewFrame = it;
  return NS_OK;
  
} // NS_NewTreeFrame


// Constructor
nsTreeFrame::nsTreeFrame()
:nsTableFrame(),mSlatedForReflow(PR_FALSE), mTwistyListener(nsnull), mGeneration(0), mUseGeneration(PR_TRUE) { }

// Destructor
nsTreeFrame::~nsTreeFrame()
{
}

NS_IMPL_ADDREF_INHERITED(nsTreeFrame, nsTableFrame)
NS_IMPL_RELEASE_INHERITED(nsTreeFrame, nsTableFrame)

NS_IMETHODIMP nsTreeFrame::QueryInterface(REFNSIID aIID, void** aInstancePtr)
{
  if (NULL == aInstancePtr) {
    return NS_ERROR_NULL_POINTER;
  }

  *aInstancePtr = NULL;
  if (aIID.Equals(NS_GET_IID(nsISelfScrollingFrame))) {
    *aInstancePtr = (void*)(nsISelfScrollingFrame*) this;
    return NS_OK;
  }

  if (aIID.Equals(NS_GET_IID(nsITreeFrame))) {
    *aInstancePtr = (void*)(nsITreeFrame*) this;
    return NS_OK;
  }

  return nsTableFrame::QueryInterface(aIID, aInstancePtr);
}

void nsTreeFrame::SetSelection(nsIPresContext* aPresContext, nsTreeCellFrame* aFrame)
{
  nsCOMPtr<nsIContent> cellContent;
  aFrame->GetContent(getter_AddRefs(cellContent));

  if (!cellContent) return;
  nsCOMPtr<nsIContent> rowContent;
  cellContent->GetParent(*getter_AddRefs(rowContent));

  if (!rowContent) return;
  nsCOMPtr<nsIContent> itemContent;
  rowContent->GetParent(*getter_AddRefs(itemContent));

  nsCOMPtr<nsIDOMXULTreeElement> treeElement = do_QueryInterface(mContent);
  nsCOMPtr<nsIDOMXULElement> cellElement = do_QueryInterface(cellContent);
  nsCOMPtr<nsIDOMXULElement> itemElement = do_QueryInterface(itemContent);

  nsCOMPtr<nsIAtom> kSuppressSelectChange = dont_AddRef(NS_NewAtom("suppressonselect"));
  mContent->SetAttribute(kNameSpaceID_None, kSuppressSelectChange, "true", PR_FALSE);
  treeElement->SelectItem(itemElement);
  mContent->UnsetAttribute(kNameSpaceID_None, kSuppressSelectChange, PR_FALSE);
  treeElement->SelectCell(cellElement);
}

void nsTreeFrame::ToggleSelection(nsIPresContext* aPresContext, nsTreeCellFrame* aFrame)
{
	nsCOMPtr<nsIContent> cellContent;
  aFrame->GetContent(getter_AddRefs(cellContent));

  nsCOMPtr<nsIContent> rowContent;
  cellContent->GetParent(*getter_AddRefs(rowContent));

  nsCOMPtr<nsIContent> itemContent;
  rowContent->GetParent(*getter_AddRefs(itemContent));

  nsCOMPtr<nsIDOMXULTreeElement> treeElement = do_QueryInterface(mContent);
  nsCOMPtr<nsIDOMXULElement> cellElement = do_QueryInterface(cellContent);
  nsCOMPtr<nsIDOMXULElement> itemElement = do_QueryInterface(itemContent);

  nsCOMPtr<nsIAtom> kSuppressSelectChange = dont_AddRef(NS_NewAtom("suppressonselect"));
  mContent->SetAttribute(kNameSpaceID_None, kSuppressSelectChange, "true", PR_FALSE);
  treeElement->ToggleItemSelection(itemElement);
  mContent->UnsetAttribute(kNameSpaceID_None, kSuppressSelectChange, PR_FALSE);
  treeElement->ToggleCellSelection(cellElement);
}

void nsTreeFrame::RangedSelection(nsIPresContext* aPresContext, nsTreeCellFrame* pEndFrame)
{
	nsCOMPtr<nsIContent> endCellContent;
	pEndFrame->GetContent(getter_AddRefs(endCellContent));
	if (!endCellContent)
		return;

	nsCOMPtr<nsIContent> endRowContent;
	endCellContent->GetParent(*getter_AddRefs(endRowContent));
	if (!endRowContent)
		return;

	nsCOMPtr<nsIContent> endItemContent;
	endRowContent->GetParent(*getter_AddRefs(endItemContent));
	if (!endItemContent)
		return;

	nsCOMPtr<nsIContent> endParent;
	endItemContent->GetParent(*getter_AddRefs(endParent));
	if (!endParent)
		return;

	nsCOMPtr<nsIDOMXULTreeElement> treeElement = do_QueryInterface(mContent);
	nsIDOMNodeList* selectedItems;
	treeElement->GetSelectedItems(&selectedItems);
	if (!selectedItems)
		return;

	PRUint32 length;
	selectedItems->GetLength(&length);
	if (length < 1)
		return;

	nsCOMPtr<nsIDOMNode> domNode;
	selectedItems->Item(0, getter_AddRefs(domNode));
	nsCOMPtr<nsIContent> startItemContent = do_QueryInterface(domNode);

	nsCOMPtr<nsIContent> startParent;
	startItemContent->GetParent(*getter_AddRefs(startParent));
	if (!startParent)
		return;

	// Get a range so we can create an iterator
	nsCOMPtr<nsIDOMRange> range;
	nsresult result;
	result = nsComponentManager::CreateInstance(kCRangeCID, nsnull, 
						nsIDOMRange::GetIID(), getter_AddRefs(range));

	PRInt32 startIndex = 0;
	PRInt32 endIndex = 0;
	startParent->IndexOf(startItemContent, startIndex);
	endParent->IndexOf(endItemContent, endIndex);

	nsCOMPtr<nsIDOMNode> startDOMNode = do_QueryInterface(startParent);
	nsCOMPtr<nsIDOMNode> endDOMNode = do_QueryInterface(endParent);
	result = range->SetStart(startDOMNode, startIndex);
	result = range->SetEnd(endDOMNode, endIndex+1);
	if (NS_FAILED(result))
	{
		// Ranges need to be increasing, try reversing directions
		result = range->SetStart(endDOMNode, endIndex);
		result = range->SetEnd(startDOMNode, startIndex+1);
		if (NS_FAILED(result))
			return;
	}

	// Create the iterator
	nsCOMPtr<nsIContentIterator> iter;
	result = nsComponentManager::CreateInstance(kCContentIteratorCID, nsnull,
																							nsIContentIterator::GetIID(), 
																							getter_AddRefs(iter));
	if (NS_FAILED(result))
    return; // result;

	// Iterate and select
	nsCOMPtr<nsIAtom> treeItemAtom = dont_AddRef(NS_NewAtom("treeitem"));
	nsCOMPtr<nsIAtom> selectedAtom = dont_AddRef(NS_NewAtom("selected"));
	nsCOMPtr<nsIAtom> suppressSelectAtom = dont_AddRef(NS_NewAtom("suppressonselect"));
	nsAutoString trueString("true", 4);
	nsCOMPtr<nsIContent> content = nsnull;
	nsCOMPtr<nsIAtom> tag;

	iter->Init(range);
	result = iter->First();
	while (NS_SUCCEEDED(result) && NS_ENUMERATOR_FALSE == iter->IsDone())
	{
		result = iter->CurrentNode(getter_AddRefs(content));
		if (NS_FAILED(result) || !content)
			return; // result;

		// If tag==item, Do selection stuff
    content->GetTag(*getter_AddRefs(tag));
    if (tag && tag == treeItemAtom)
    {
			content->SetAttribute(kNameSpaceID_None, selectedAtom, 
														trueString, /*aNotify*/ PR_TRUE);
		}

		result = iter->Next();
		// Deal with closed nodes here
		// Also had strangeness where parent of selected subrange was selected even 
		// though it wasn't in the range.
	}
}

void
nsTreeFrame::GetTreeBody(nsTreeRowGroupFrame** aResult)
{
  nsIFrame* curr = mFrames.FirstChild();
  while (curr) {
    nsCOMPtr<nsIContent> content;
    curr->GetContent(getter_AddRefs(content));
    if (content) {
      nsCOMPtr<nsIAtom> tag;
      content->GetTag(*getter_AddRefs(tag));
      if (tag && tag.get() == nsXULAtoms::treechildren) {
        // This is our actual treechildren frame.
        nsTreeRowGroupFrame* rowGroup = (nsTreeRowGroupFrame*)curr; // XXX I am evil.
        *aResult = rowGroup;
        return;
      }
    }

    curr->GetNextSibling(&curr);
  }
}

NS_IMETHODIMP 
nsTreeFrame::HandleEvent(nsIPresContext* aPresContext, 
                             nsGUIEvent*     aEvent,
                             nsEventStatus*  aEventStatus)
{
  NS_ENSURE_ARG_POINTER(aEventStatus);
  *aEventStatus = nsEventStatus_eConsumeDoDefault;
  if (aEvent->message == NS_KEY_PRESS) {
    nsKeyEvent* keyEvent = (nsKeyEvent*)aEvent;
    PRUint32 keyCode = keyEvent->keyCode;
    if (keyCode == NS_VK_UP ||
        keyCode == NS_VK_DOWN ||
        keyCode == NS_VK_LEFT ||
        keyCode == NS_VK_RIGHT ||
        keyCode == NS_VK_ENTER) {

      // Get our treechildren child frame.
      nsTreeRowGroupFrame* treeRowGroup = nsnull;
      GetTreeBody(&treeRowGroup);

      if (!treeRowGroup)
        return NS_OK; // No tree body. Just bail.

      nsCOMPtr<nsIDOMXULTreeElement> treeElement = do_QueryInterface(mContent);
      nsCOMPtr<nsIDOMNodeList> itemNodeList;
      nsCOMPtr<nsIDOMNodeList> cellNodeList;
      treeElement->GetSelectedItems(getter_AddRefs(itemNodeList));
      treeElement->GetSelectedCells(getter_AddRefs(cellNodeList));
      PRUint32 itemLength;
      PRUint32 cellLength;
      itemNodeList->GetLength(&itemLength);
      cellNodeList->GetLength(&cellLength);

      PRInt32 rowIndex = -1;
      PRInt32 cellIndex = 0;

      if (cellLength != 0 && itemLength == 0) {
        nsCOMPtr<nsIDOMNode> node;
        cellNodeList->Item(0, getter_AddRefs(node));
        nsCOMPtr<nsIContent> content = do_QueryInterface(node);
        treeRowGroup->IndexOfCell(aPresContext, content, rowIndex, cellIndex);
      }
      else if (cellLength == 0 && itemLength != 0) {
        nsCOMPtr<nsIDOMNode> node;
        itemNodeList->Item(0, getter_AddRefs(node));
        nsCOMPtr<nsIContent> content = do_QueryInterface(node);
        treeRowGroup->IndexOfRow(aPresContext, content, rowIndex);
      }
      else if (cellLength != 0 && itemLength != 0) {
        nsCOMPtr<nsIDOMNode> node;
        cellNodeList->Item(0, getter_AddRefs(node));
        nsCOMPtr<nsIContent> content = do_QueryInterface(node);
        treeRowGroup->IndexOfCell(aPresContext, content, rowIndex, cellIndex);
      }
      
      // We now have a valid row and cell index for the current selection.  Based on the
      // direction, let's adjust the row and column index.
      if (rowIndex == -1)
        rowIndex = 0;
      else if (keyCode == NS_VK_DOWN)
        rowIndex++;
      else if (keyCode == NS_VK_UP)
        rowIndex--;

      // adjust for zero-based mRowCount
      nsTableRowFrame* firstRow=nsnull;
      treeRowGroup->GetFirstRowFrame(&firstRow);
      if (firstRow) {
        PRInt32 rowNumber = rowIndex - firstRow->GetRowIndex();

        if (!treeRowGroup->IsValidRow(rowNumber))
          return NS_OK;

        // Ensure that the required index is visible.
        treeRowGroup->EnsureRowIsVisible(rowNumber);
      }

      // Now that the row is scrolled into view, we have a frame created. We can retrieve the cell.
      nsTreeCellFrame* cellFrame=nsnull;
      treeRowGroup->GetCellFrameAtIndex(rowIndex, cellIndex, &cellFrame);
      if (!cellFrame)
        return NS_OK; // No cell. Whatever. Bail.

      // We got it! Perform the selection on an up/down.
      if (keyCode == NS_VK_UP || keyCode == NS_VK_DOWN)
        SetSelection(aPresContext, cellFrame);
      else if (keyCode == NS_VK_ENTER || keyCode == NS_VK_RETURN)
        cellFrame->ToggleOpenClose();
      else if (keyCode == NS_VK_LEFT)
        cellFrame->Close();
      else if (keyCode == NS_VK_RIGHT)
        cellFrame->Open();
    }
  }
  return NS_OK;
}
  
void nsTreeFrame::MoveUp(nsIPresContext* aPresContext, nsTreeCellFrame* pFrame)
{
	PRInt32 rowIndex;
  pFrame->GetRowIndex(rowIndex);
	PRInt32 colIndex;
  pFrame->GetColIndex(colIndex);
	if (rowIndex > 0)
	{
		MoveToRowCol(aPresContext, rowIndex-1, colIndex);
	}
}

void nsTreeFrame::MoveDown(nsIPresContext* aPresContext, nsTreeCellFrame* pFrame)
{
	PRInt32 rowIndex;
  pFrame->GetRowIndex(rowIndex);
	PRInt32 colIndex;
  pFrame->GetColIndex(colIndex);
	PRInt32 totalRows = mCellMap->GetRowCount();

	if (rowIndex < totalRows-1)
	{
		MoveToRowCol(aPresContext, rowIndex+1, colIndex);
	}
}

void nsTreeFrame::MoveLeft(nsIPresContext* aPresContext, nsTreeCellFrame* pFrame)
{
	PRInt32 rowIndex;
  pFrame->GetRowIndex(rowIndex);
	PRInt32 colIndex;
  pFrame->GetColIndex(colIndex);
	if (colIndex > 0)
	{
		MoveToRowCol(aPresContext, rowIndex, colIndex-1);
	}
}

void nsTreeFrame::MoveRight(nsIPresContext* aPresContext, nsTreeCellFrame* aFrame)
{
	PRInt32 rowIndex;
  aFrame->GetRowIndex(rowIndex);
	PRInt32 colIndex;
  aFrame->GetColIndex(colIndex);
	PRInt32 totalCols = mCellMap->GetColCount();

	if (colIndex < totalCols-1)
	{
		MoveToRowCol(aPresContext, rowIndex, colIndex+1);
	}
}

void nsTreeFrame::MoveToRowCol(nsIPresContext* aPresContext, PRInt32 aRow, PRInt32 aCol)
{
	nsTableCellFrame* cellFrame = mCellMap->GetCellInfoAt(aRow, aCol);

	// We now have the cell that should be selected. 
	nsTreeCellFrame* treeCell = NS_STATIC_CAST(nsTreeCellFrame*, cellFrame);
	SetSelection(aPresContext, treeCell);
}

NS_IMETHODIMP 
nsTreeFrame::Destroy(nsIPresContext* aPresContext)
{
  nsCOMPtr<nsIDOMEventReceiver> target = do_QueryInterface(mContent);
  target->RemoveEventListener("mousedown", mTwistyListener, PR_TRUE); 
	mTwistyListener = nsnull;
  return nsTableFrame::Destroy(aPresContext);
}

NS_IMETHODIMP
nsTreeFrame::Reflow(nsIPresContext*          aPresContext,
							      nsHTMLReflowMetrics&     aDesiredSize,
							      const nsHTMLReflowState& aReflowState,
							      nsReflowStatus&          aStatus)
{
  NS_ASSERTION(aReflowState.mComputedWidth != NS_UNCONSTRAINEDSIZE, 
               "Reflowing tree with unconstrained width!!!!");
  
  NS_ASSERTION(aReflowState.mComputedHeight != NS_UNCONSTRAINEDSIZE, 
               "Reflowing tree with unconstrained height!!!!");

  //printf("Tree Width: %d, Tree Height: %d\n", aReflowState.mComputedWidth, aReflowState.mComputedHeight);

  nsresult rv = NS_OK;

  mSlatedForReflow = PR_FALSE;
  
  if (!mSuppressReflow) {
    nsRect rect;
    GetRect(rect);
    if (rect.width != aReflowState.mComputedWidth && aReflowState.reason == eReflowReason_Resize) {
      // We're doing a resize and changing the width of the table. All rows must
      // reflow. Reset our generation.
      SetUseGeneration(PR_FALSE);
    }
  
    if (UseGeneration()) {
      ++mGeneration;
    }

    rv = nsTableFrame::Reflow(aPresContext, aDesiredSize, aReflowState, aStatus);
  }
  else
  {
    aStatus = NS_FRAME_COMPLETE;
  }

  if (aReflowState.mComputedWidth != NS_UNCONSTRAINEDSIZE) 
    aDesiredSize.width = aReflowState.mComputedWidth + 
        aReflowState.mComputedBorderPadding.left + aReflowState.mComputedBorderPadding.right;
  
  if (aReflowState.mComputedHeight != NS_UNCONSTRAINEDSIZE)
    aDesiredSize.height = aReflowState.mComputedHeight +
      aReflowState.mComputedBorderPadding.top + aReflowState.mComputedBorderPadding.bottom;

  aDesiredSize.ascent = aDesiredSize.height;
  
  if (!UseGeneration())
    SetUseGeneration(PR_TRUE);

  return rv;
}

NS_IMETHODIMP
nsTreeFrame::DidReflow(nsIPresContext*   aPresContext,
                        nsDidReflowStatus aStatus)
{
  nsresult rv = nsTableFrame::DidReflow(aPresContext, aStatus);
  return rv;
}

NS_IMETHODIMP
nsTreeFrame::MarkForDirtyReflow(nsIPresContext* aPresContext)
{
  mSuppressReflow = PR_FALSE;
  InvalidateFirstPassCache();
  nsCOMPtr<nsIPresShell> shell;
  aPresContext->GetShell(getter_AddRefs(shell));
  nsFrameState      frameState;
  nsIFrame*         tableParentFrame;
  nsIReflowCommand* reflowCmd;
 
  // Mark the table frame as dirty
  GetFrameState(&frameState);
  frameState |= NS_FRAME_IS_DIRTY;
  SetFrameState(frameState);

  // Target the reflow comamnd at its parent frame
  GetParent(&tableParentFrame);
  nsresult rv = NS_NewHTMLReflowCommand(&reflowCmd, tableParentFrame,
                                        nsIReflowCommand::ReflowDirty);
  if (NS_SUCCEEDED(rv)) {
    // Add the reflow command
    rv = shell->AppendReflowCommand(reflowCmd);
    NS_RELEASE(reflowCmd);
  }
  return rv;
}

NS_IMETHODIMP
nsTreeFrame::Init(nsIPresContext*  aPresContext,
                  nsIContent*      aContent,
                  nsIFrame*        aParent,
                  nsIStyleContext* aContext,
                  nsIFrame*        aPrevInFlow)
{
  nsresult  rv = nsTableFrame::Init(aPresContext, aContent, aParent, aContext, aPrevInFlow);

  // Create the menu bar listener.
  mTwistyListener = new nsTreeTwistyListener();

  nsCOMPtr<nsIDOMEventReceiver> target = do_QueryInterface(mContent);
  
  target->AddEventListener("mousedown", mTwistyListener, PR_TRUE); 

  return rv;
}

PRBool
nsTreeFrame::ContainsFlexibleColumn(PRInt32 aStartIndex, PRInt32 aEndIndex, 
                                    nsTableColFrame** aResult)
{
  for (PRInt32 i = aEndIndex; i >= aStartIndex; i--) {
    nsTableColFrame* result = GetColFrame(i);
    nsCOMPtr<nsIContent> colContent;
    result->GetContent(getter_AddRefs(colContent));
    nsCOMPtr<nsIAtom> fixedAtom = dont_AddRef(NS_NewAtom("fixed"));
    if (colContent) {
      nsAutoString fixedValue;
      colContent->GetAttribute(kNameSpaceID_None, fixedAtom, fixedValue);
      if (fixedValue != "true") {
        // We are a proportional column.
        if (aResult)
          *aResult = result;
        return PR_TRUE;
      }
    }
  }

  return PR_FALSE;
}

PRInt32
nsTreeFrame::GetInsertionIndex(nsIFrame *aFrame)
{
  nsIFrame *child = mFrames.FirstChild();

  PRInt32 index=0;
  while (child) {
    if (child==aFrame) {
      return index;
    }
    const nsStyleDisplay *display;
    child->GetStyleData(eStyleStruct_Display,
                        (const nsStyleStruct*&)display);

    if (IsRowGroup(display->mDisplay)) {
        PRBool done=PR_FALSE;
        index = ((nsTreeRowGroupFrame*)child)->GetInsertionIndex(aFrame, index, done);
        if (done) return index;
    } 
    child->GetNextSibling(&child);
  }
  return index;
}


NS_IMETHODIMP
nsTreeFrame::ScrollByLines(nsIPresContext* aPresContext, PRInt32 lines)
{
  // Get our treechildren child frame.
  nsTreeRowGroupFrame* treeRowGroup = nsnull;
  GetTreeBody(&treeRowGroup);
  
  if (!treeRowGroup)
    return NS_OK; // No tree body. Just bail.

  treeRowGroup->ScrollByLines(aPresContext, lines);
  return NS_OK;
}

NS_IMETHODIMP
nsTreeFrame::ScrollByPages(nsIPresContext* aPresContext, PRInt32 pages)
{
  PRInt32 lines;

  // Get our treechildren child frame.
  nsTreeRowGroupFrame* treeRowGroup = nsnull;
  GetTreeBody(&treeRowGroup);
  
  if (!treeRowGroup)
    return NS_OK; // No tree body. Just bail.

  PRInt32 absPages = (pages > 0) ? pages : -pages;
  PRInt32 treeRows;
  treeRowGroup->GetRowCount(treeRows);

  lines = (absPages * treeRows) - 1;
  if (pages < 0)
    lines = -lines;

#ifdef DEBUG_bryner
  printf("nsTreeFrame::ScrollByPages : scrolling treeRowGroup by %d lines\n", lines);
  treeRowGroup->ScrollByLines(aPresContext, lines);
#endif

  return NS_OK;
}

NS_IMETHODIMP
nsTreeFrame::CollapseScrollbar(nsIPresContext* aPresContext, PRBool aHide)
{
  // Get our treechildren child frame.
  nsTreeRowGroupFrame* treeRowGroup = nsnull;
  GetTreeBody(&treeRowGroup);
  
  if (!treeRowGroup)
    return NS_OK; // No tree body. Just bail.

  treeRowGroup->CollapseScrollbar(aHide, aPresContext, nsnull);

  return NS_OK;
}


NS_IMETHODIMP
nsTreeFrame::EnsureRowIsVisible(PRInt32 aRowIndex)
{
  // Get our treechildren child frame.
  nsTreeRowGroupFrame* treeRowGroup = nsnull;
  GetTreeBody(&treeRowGroup);

  if (!treeRowGroup) return NS_OK;
  
  treeRowGroup->EnsureRowIsVisible(aRowIndex);
  return NS_OK;
}
