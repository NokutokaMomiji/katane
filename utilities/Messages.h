#ifndef KATANE_MESSAGES_H
#define KATANE_MESSAGES_H

#include "Common.h"

#define KTN_PLAIN(text) text, text

#define KTN_MESSAGE(vm, ...)                KTN_MSG_SELECT_((vm), __VA_ARGS__)
#define KTN_MSG_SELECT_(vm, boring, katane) ((vm)->kataneMode ? (katane) : (boring))

<%s>

#define KTN_CTX_RETURN_VALUE        "Return type error"
#define KTN_CTX_TOSTRING_RETURN     "toString return type error"
#define KTN_CTX_HASHCODE_RETURN     "hashCode return type error"
#define KTN_CTX_GLOBAL_VARIABLE     "Global mochi type error"
#define KTN_CTX_LOCAL_VARIABLE      "Local mochi type error"
#define KTN_CTX_PROPERTY            "Property type error"
#define KTN_CTX_PARAMETER           "Parameter type error"
#define KTN_CTX_TYPE_ANNOTATION     "Type error"
#define KTN_CTX_INDEX               "Index type error"
#define KTN_CTX_LENGTH_ARGUMENT     "length"
#define KTN_CTX_POSITIONAL_ARGUMENT "Argument %d"
#define KTN_CTX_NAMED_ARGUMENT      "Argument \"%s\""

#endif
