/**	
 * |----------------------------------------------------------------------
 * | Copyright (c) 2017 Tilen Majerle
 * |  
 * | Permission is hereby granted, free of charge, to any person
 * | obtaining a copy of this software and associated documentation
 * | files (the "Software"), to deal in the Software without restriction,
 * | including without limitation the rights to use, copy, modify, merge,
 * | publish, distribute, sublicense, and/or sell copies of the Software, 
 * | and to permit persons to whom the Software is furnished to do so, 
 * | subject to the following conditions:
 * | 
 * | The above copyright notice and this permission notice shall be
 * | included in all copies or substantial portions of the Software.
 * | 
 * | THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * | EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * | OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
 * | AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * | HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * | WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * | FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * | OTHER DEALINGS IN THE SOFTWARE.
 * |----------------------------------------------------------------------
 */
#define GUI_INTERNAL
#include "gui_dialog.h"

/******************************************************************************/
/******************************************************************************/
/***                           Private structures                            **/
/******************************************************************************/
/******************************************************************************/
typedef struct DDList_t {
    GUI_LinkedList_t list;                          /*!< Linked list entry element, must always be first on list */
    GUI_ID_t id;                                    /*!< Dialog ID */
    GUI_HANDLE_p h;                                 /*!< Pointer to dialog address */
    volatile int status;                            /*!< Status on dismissed call */
#if GUI_OS
    gui_sys_sem_t sem;                              /*!< Semaphore handle for blocking */
    uint8_t ib;                                     /*!< Indication if dialog is blocking */
#endif /* GUI_OS */
} DDList_t;

/******************************************************************************/
/******************************************************************************/
/***                           Private definitions                           **/
/******************************************************************************/
/******************************************************************************/
#define __GW(x)             ((GUI_WINDOW_t *)(x))

static
uint8_t GUI_DIALOG_Callback(GUI_HANDLE_p h, GUI_WC_t ctrl, void* param, void* result);

static
GUI_LinkedListRoot_t DDList;                        /*!< List of dismissed dialog elements which are not read yet */

/******************************************************************************/
/******************************************************************************/
/***                            Private variables                            **/
/******************************************************************************/
/******************************************************************************/
static const GUI_WIDGET_t Widget = {
    .Name = _GT("DIALOG"),                          /*!< Widget name */
    .Size = sizeof(GUI_DIALOG_t),                   /*!< Size of widget for memory allocation */
    .Flags = GUI_FLAG_WIDGET_ALLOW_CHILDREN | GUI_FLAG_WIDGET_DIALOG_BASE,  /*!< List of widget flags */
    .Callback = GUI_DIALOG_Callback,                /*!< Control function */
    .Colors = NULL,                                 /*!< Pointer to colors array */
    .ColorsCount = 0,                               /*!< Number of colors */
};

/******************************************************************************/
/******************************************************************************/
/***                            Private functions                            **/
/******************************************************************************/
/******************************************************************************/

/* Add widget to active dialogs (not yet dismissed) */
static
DDList_t* __AddToActiveDialogs(GUI_HANDLE_p h) {
    DDList_t* l;
    
    l = __GUI_MEMALLOC(sizeof(*l));                 /* Allocate memory for dismissed dialog list */
    if (l) {
        l->h = h;
        l->id = __GUI_WIDGET_GetId(h);
        __GUI_LINKEDLIST_ADD_GEN(&DDList, &l->list);/* Add entry to linked list */
    }
    return l;
}

/* Remove and free memory from linked list */
static
void __RemoveFromActiveDialogs(DDList_t* l) {
    __GUI_LINKEDLIST_REMOVE_GEN(&DDList, &l->list); /* Remove entry from linked list first */
    
    __GUI_MEMFREE(l);                               /* Free memory */
}

/* Get entry from linked list for specific dialog */
static
DDList_t* __GetFromActiveDialogs(GUI_HANDLE_p h) {
    DDList_t* l = NULL;
    GUI_ID_t id;
    
    id = __GUI_WIDGET_GetId(h);                     /* Get id of widget */
    
    for (l = (DDList_t *)__GUI_LINKEDLIST_GETNEXT_GEN((GUI_LinkedListRoot_t *)&DDList, NULL); l;
        l = (DDList_t *)__GUI_LINKEDLIST_GETNEXT_GEN(NULL, (GUI_LinkedList_t *)l)) {
        if (l->h == h && l->id == id) {             /* Check match for handle and id */
            break;
        }
    }
    return l;
}

/* Default dialog callback */
static
uint8_t GUI_DIALOG_Callback(GUI_HANDLE_p h, GUI_WC_t ctrl, void* param, void* result) {
    __GUI_ASSERTPARAMS(h && __GH(h)->Widget == &Widget);    /* Check input parameters */
    switch (ctrl) {                                 /* Handle control function if required */
        case GUI_WC_Remove: {
            /* Remove from dismissed list if exists */
            return 1;
        }
        case GUI_WC_PreInit: {
            
            return 1;
        }
        case GUI_WC_OnDismiss: {
            int dv = *(int *)param;                 /* Get dismiss parameter value */
            __GUI_UNUSED(dv);                       /* Unused parameters */
            return 1;
        }
        default:                                    /* Handle default option */
            __GUI_UNUSED3(h, param, result);        /* Unused elements to prevent compiler warnings */
            return 0;                               /* Command was not processed */
    }
}

/******************************************************************************/
/******************************************************************************/
/***                                Public API                               **/
/******************************************************************************/
/******************************************************************************/
GUI_HANDLE_p GUI_DIALOG_Create(GUI_ID_t id, GUI_iDim_t x, GUI_iDim_t y, GUI_Dim_t width, GUI_Dim_t height, GUI_HANDLE_p parent, GUI_WIDGET_CALLBACK_t cb, uint16_t flags) {
    GUI_HANDLE_p ptr;
    __GUI_ENTER();                                  /* Enter GUI */
    
    ptr = __GUI_WIDGET_Create(&Widget, id, x, y, width, height, parent, cb, flags | GUI_FLAG_WIDGET_CREATE_PARENT_DESKTOP); /* Allocate memory for basic widget */
    if (ptr) {
        __AddToActiveDialogs(ptr);                  /* Add this dialog to active dialogs */
    }
    
    __GUI_LEAVE();                                  /* Leave GUI */
    return (GUI_HANDLE_p)ptr;
}

#if GUI_OS
int GUI_DIALOG_CreateBlocking(GUI_ID_t id, GUI_iDim_t x, GUI_iDim_t y, GUI_Dim_t width, GUI_Dim_t height, GUI_HANDLE_p parent, GUI_WIDGET_CALLBACK_t cb, uint16_t flags) {
    GUI_HANDLE_p ptr;
    int resp = -1;                                  /* Dialog not created error */
    
    __GUI_ENTER();                                  /* Enter GUI */
    
    ptr = GUI_DIALOG_Create(id, x, y, width, height, parent, cb, flags);    /* Create dialog first */
    if (ptr) {                                      /* Widget created */
        DDList_t* l;
        l = __GetFromActiveDialogs(ptr);            /* Get entry from active dialogs */
        if (l) {                                    /* Check if successfully added widget to active dialogs */
            l->ib = 1;                              /* Blocking entry */
            if (!gui_sys_sem_create(&l->sem, 0)) {  /* Create semaphore and lock it immediatelly */
                gui_sys_sem_wait(&l->sem, 0);       /* Wait for semaphore again, should be released from dismiss function */
                gui_sys_sem_release(&l->sem);       /* Release semaphore */
                gui_sys_sem_delete(&l->sem);        /* Delete system semaphore */
                resp = l->status;                   /* Get new status */
                __RemoveFromActiveDialogs(l);       /* Remove from active dialogs */
            } else {
                __GUI_WIDGET_Remove(ptr);           /* Remove widget */
            }
        } else {
            __GUI_WIDGET_Remove(ptr);               /* Remove widget */
        }
    }
    
    __GUI_LEAVE();                                  /* Leave GUI */
    return resp;
}
#endif /* GUI_OS */

uint8_t GUI_DIALOG_Dismiss(GUI_HANDLE_p h, int status) {
    DDList_t* l;
    uint8_t ret = 0;
    
    __GUI_ASSERTPARAMS(h && __GH(h)->Widget == &Widget);    /* Check input parameters */
    __GUI_ENTER();                                  /* Enter GUI */
    
    l = __GetFromActiveDialogs(h);                  /* Get entry from list */
    if (l) {
        l->status = status;                         /* Save status for later */
        __GUI_WIDGET_Callback(h, GUI_WC_OnDismiss, (int *)&l->status, 0);   /* Process callback */
#if GUI_OS
        if (l->ib && !gui_sys_sem_isvalid(&l->sem)) {   /* Check if semaphore is valid */
            gui_sys_sem_release(&l->sem);           /* Release locked semaphore */
        } else 
#endif /* GUI_OS */
        {
            __RemoveFromActiveDialogs(l);           /* Remove from active dialogs */
        }
        __GUI_WIDGET_Remove(h);                     /* Remove widget */
    }
    
    __GUI_LEAVE();                                  /* Leave GUI */
    return ret;
}
