/* Automatically generated nanopb header */
/* Generated by nanopb-0.4.5 */

#ifndef PB_CONFIG_PB_H_INCLUDED
#define PB_CONFIG_PB_H_INCLUDED
#include <pb.h>
#include "product.pb.h"

#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

/* Struct definitions */
typedef struct _ConfigMessage { 
    char name[41]; 
    int32_t list_id; 
    pb_size_t products_count;
    Product products[9]; 
} ConfigMessage;


#ifdef __cplusplus
extern "C" {
#endif

/* Initializer values for message structs */
#define ConfigMessage_init_default               {"", 0, 0, {Product_init_default, Product_init_default, Product_init_default, Product_init_default, Product_init_default, Product_init_default, Product_init_default, Product_init_default, Product_init_default}}
#define ConfigMessage_init_zero                  {"", 0, 0, {Product_init_zero, Product_init_zero, Product_init_zero, Product_init_zero, Product_init_zero, Product_init_zero, Product_init_zero, Product_init_zero, Product_init_zero}}

/* Field tags (for use in manual encoding/decoding) */
#define ConfigMessage_name_tag                   1
#define ConfigMessage_list_id_tag                2
#define ConfigMessage_products_tag               3

/* Struct field encoding specification for nanopb */
#define ConfigMessage_FIELDLIST(X, a) \
X(a, STATIC,   SINGULAR, STRING,   name,              1) \
X(a, STATIC,   SINGULAR, INT32,    list_id,           2) \
X(a, STATIC,   REPEATED, MESSAGE,  products,          3)
#define ConfigMessage_CALLBACK NULL
#define ConfigMessage_DEFAULT NULL
#define ConfigMessage_products_MSGTYPE Product

extern const pb_msgdesc_t ConfigMessage_msg;

/* Defines for backwards compatibility with code written before nanopb-0.4.0 */
#define ConfigMessage_fields &ConfigMessage_msg

/* Maximum encoded size of messages (where known) */
#if defined(Product_size)
#define ConfigMessage_size                       (107 + 9*Product_size)
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
