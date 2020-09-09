/*
 * Copyright(c) 2020 ADLINK Technology Limited and others
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License v. 2.0 which is available at
 * http://www.eclipse.org/legal/epl-2.0, or the Eclipse Distribution License
 * v. 1.0 which is available at
 * http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * SPDX-License-Identifier: EPL-2.0 OR BSD-3-Clause
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "idlcxx/streamer_generator.h"
#include "idlcxx/backendCpp11Utils.h"
#include "idl/tree.h"
#include "idl/string.h"

#define format_ostream_indented(depth,ostr,_str,...) \
if (depth > 0) format_ostream(ostr, "%*c", depth, ' '); \
format_ostream(ostr, _str, ##__VA_ARGS__);

#define format_write_stream(indent,ctx,_str,...) \
format_ostream_indented(indent ? ctx->depth*2 : 0, ctx->write_stream, _str, ##__VA_ARGS__);

#define format_write_size_stream(indent,ctx,_str,...) \
format_ostream_indented(indent ? ctx->depth*2 : 0, ctx->write_size_stream, _str, ##__VA_ARGS__);

#define format_read_stream(indent,ctx,_str,...) \
format_ostream_indented(indent ? ctx->depth*2 : 0, ctx->read_stream, _str, ##__VA_ARGS__);

#define format_header_stream(indent,ctx,_str,...) \
format_ostream_indented(indent ? ctx->depth*2 : 0, ctx->str->head_stream, _str, ##__VA_ARGS__);

static const char* struct_write_func_fmt = "size_t %s::write_struct(void *data, size_t position) const";
static const char* union_switch_fmt = "  switch (_d())\n";
static const char* union_case_fmt = "  case %s:\n";
static const char* default_case_fmt = "  default:\n";
static const char* union_case_ending = "  break;\n";
static const char* union_clear_func = "  clear();\n";
static const char* primitive_calc_alignment_modulo_fmt = "(%d - position%%%d)%%%d;";
static const char* primitive_calc_alignment_shift_fmt = "(%d - position&%#x)&%#x;";
static const char* primitive_incr_fmt = "  position += ";
static const char* primitive_incr_alignment_fmt = "  position += alignmentbytes;";
static const char* primitive_write_func_padding_fmt = "  memset((char*)data+position,0x0,%d);  //setting padding bytes to 0x0\n";
static const char* primitive_write_func_alignment_fmt = "  memset((char*)data+position,0x0,alignmentbytes);  //setting alignment bytes to 0x0\n";
static const char* primitive_write_func_write_fmt = "  *((%s*)((char*)data+position)) = %s;  //writing bytes for member: %s\n";
static const char* primitive_write_func_array_fmt = "  memcpy((char*)data+position,%s.data(),%d);  //writing bytes for member: %s\n";
static const char* primitive_write_func_seq_fmt = "sequenceentries = %s%s;  //number of entries in the sequence\n";
static const char* primitive_write_func_seq2_fmt = "  *((uint32_t*)((char*)data + position)) = sequenceentries;  //writing bytes for member: %s\n";
static const char* incr_comment = "  //moving position indicator\n";
static const char* align_comment = "  //alignment\n";
static const char* padding_comment = "  //padding bytes\n";
static const char* instance_write_func_fmt = "  position = %s.write_struct(data, position);\n";
static const char* instance_write_array_fmt = "  for (size_t _i = 0; _i < %d; _i++) position = %s[_i].write_struct(data, position);\n";
static const char* namespace_declaration_fmt = "namespace %s\n";
static const char* namespace_closure_fmt = "} //end namespace %s\n\n";
static const char* struct_write_size_func_fmt = "size_t %s::write_size(size_t offset) const";
static const char* primitive_incr_pos = "  position += %d;";
static const char* instance_size_func_calc_fmt = "  position += %s.write_size(position);\n";
static const char* instance_size_array_calc_fmt = "  for (size_t _i = 0; _i < %d; _i++) position += %s[_i].write_size(position);\n";
static const char* struct_read_func_fmt = "size_t %s::read_struct(void *data, size_t position)";
static const char* primitive_read_func_read_fmt = "  %s = *((%s*)((char*)data+position));  //reading bytes for member: %s\n";
static const char* primitive_read_func_array_fmt = "  memcpy(%s.data(),(char*)data+position,%d);  //reading bytes for member: %s\n";
static const char* primitive_read_func_seq_fmt = "sequenceentries = *((%s*)((char*)data+position));  //number of entries in the sequence\n";
static const char* instance_read_func_fmt = "  position = %s.read_struct(data, position);\n";
static const char* instance_read_array_fmt = "  for (size_t _i = 0; _i < %d; _i++) position = %s[_i].read_struct(data, position);\n";
static const char* seq_size_fmt = "%s.size()";
static const char* seq_read_resize_fmt = "  %s.resize(sequenceentries);\n";
static const char* seq_structured_write_fmt = "  for (const auto &_1:%s) position = _1.write_struct(data,position);\n";
static const char* seq_structured_write_size_fmt = "  for (const auto &_1:%s) position += _1.write_size(position);\n";
static const char* seq_structured_read_copy_fmt = "  for (size_t _1 = 0; _1 < sequenceentries; _1++) position = %s[_1].read_struct(data, position);\n";
static const char* seq_typedef_write_fmt = "  for (const auto &_1:%s) position = %swrite_typedef_%s(_1,data,position);\n";
static const char* seq_typedef_write_size_fmt = "  for (const auto &_1:%s) position += %stypedef_size_%s(_1, position);\n";
static const char* seq_typedef_read_copy_fmt = "  for (size_t _1 = 0; _1 < sequenceentries; _1++) position = %sread_typedef_%s(%s[_1], data, position);\n";
static const char* seq_primitive_write_fmt = "  memcpy((char*)data+position,%s.data(),sequenceentries*%d);  //contents for %s\n";
static const char* seq_primitive_read_fmt = "  %s.assign((%s*)((char*)data+position),(%s*)((char*)data+position)+sequenceentries);  //putting data into container\n";
static const char* seq_incr_fmt = "  position += sequenceentries*%d;";
static const char* seq_entries_fmt = "  position += (%s.size()%s)*%d;  //entries of sequence\n";
static const char* ref_cast_fmt = "dynamic_cast<%s%s&>(*this)";
static const char* member_access_fmt = "%s()";
static const char* sequence_length_exception_fmt = "  if (sequenceentries > %zu) throw dds::core::InvalidArgumentError(\"attempt to assign entries to bounded member %s in excess of maximum length %zu\");\n";
static const char* array_for_loop = "  for (size_t _i = 0; _i < %d; _i++) {\n";
static const char* typedef_write_func_fmt = "size_t write_typedef_%s(const %s &obj, void* data, size_t position)";
static const char* typedef_write_size_func_fmt = "size_t typedef_size_%s(const %s &obj, size_t offset)";
static const char* typedef_read_func_fmt = "size_t read_typedef_%s(%s &obj, void* data, size_t position)";

static const char* char_cast = "char";
static const char* bool_cast = "bool";
static const char* int8_cast = "int8_t";
static const char* uint8_cast = "uint8_t";
static const char* int16_cast = "int16_t";
static const char* uint16_cast = "uint16_t";
static const char* int32_cast = "int32_t";
static const char* uint32_cast = "uint32_t";
static const char* int64_cast = "int64_t";
static const char* uint64_cast = "uint64_t";
static const char* float_cast = "float";
static const char* double_cast = "double";
static const char* ldouble_cast = "long double";

#if 0
static const char* fixed_pt_write_digits = "    long long int digits = ((long double)obj.%s()/pow(10.0,obj.%s().fixed_scale()));\n";
static const char* fixed_pt_write_byte = "    int byte = (obj.%s().fixed_digits())/2;\n";
static const char* fixed_pt_write_fill[] = {
"    if (digits < 0)\n",
"    {\n",
"      digits *= -1;\n",
"      data[position + byte] = (digits % 10 << 4) | 0x0d;\n",
"    }\n",
"    else\n",
"    {\n",
"      data[position + byte] = (digits % 10 << 4) | 0x0c;\n",
"    }\n",
"    while (byte && digits)\n",
"    {\n",
"      byte--;\n",
"      digits /= 10;\n",
"      data[position + byte] = ((unsigned char)digits) % 10;\n",
"      digits /= 10;\n",
"      data[position + byte] |= ((unsigned char)digits) % 10 * 16;\n",
"    }\n",
"    memset(data + position,0x0,byte);\n"
};
static const char* fixed_pt_write_position = "  position += (obj.%s().fixed_digits()/2) + 1;\n";

static const char* fixed_pt_read_byte = "    int byte = obj.%s().fixed_digits()/2;\n";
static const char* fixed_pt_read_fill[] = {
"    long long int digits = ((unsigned_char)data[byte] & 0xf0) >> 4;\n",
"    if (data[byte] & 0x0d == 0x0d)\n",
"      digits *= -1;\n",
"    while (byte >= 0)\n",
"    {\n",
"      unsigned char temp = *((usigned char*)(data + position + byte));\n",
"      digits *= 10;\n",
"      digits *= 10;\n",
"      byte--;\n",
"    }\n"
};
static const char* fixed_pt_read_assign = "    obj.%s() = (pow((long double)0.1, obj.%s().fixed_scale()) * digits);\n";
static const char* fixed_pt_read_position = "    position += (obj.%s().fixed_digits()/2) + 1;\n";
#endif

struct idl_streamer_output
{
  size_t indent;
  idl_ostream_t* impl_stream;
  idl_ostream_t* head_stream;
};

typedef struct context context_t;

struct context
{
  idl_streamer_output_t* str;
  char* context;
  idl_ostream_t* write_size_stream;
  idl_ostream_t* write_stream;
  idl_ostream_t* read_stream;
  size_t depth;
  int currentalignment;
  int accumulatedalignment;
  int alignmentpresent;
  int sequenceentriespresent;
  context_t* parent;
};

static uint64_t array_entries(idl_declarator_t* decl);
static idl_retcode_t add_default_case(context_t* ctx);
static idl_retcode_t process_node(context_t* ctx, idl_node_t* node);
static idl_retcode_t process_instance(context_t* ctx, idl_declarator_t* decl, idl_type_spec_t* spec);
static idl_retcode_t process_struct(context_t* ctx, idl_declarator_t* decl, idl_struct_t* spec);
static idl_retcode_t process_base(context_t* ctx, idl_declarator_t* decl, idl_type_spec_t* spec);
static idl_retcode_t process_template(context_t* ctx, idl_declarator_t* decl, idl_type_spec_t* spec);
static idl_type_spec_t* resolve_typedef(idl_type_spec_t* def);
static idl_retcode_t process_typedef_definition(context_t* ctx, idl_typedef_t* node);
static idl_retcode_t process_typedef_instance(context_t* ctx, idl_declarator_t* decl, idl_type_spec_t* spec);
static idl_retcode_t process_known_width(context_t* ctx, const char* accessor, idl_mask_t typespec, int sequence, const char *seqsizeappend);
static idl_retcode_t process_known_width_array(context_t* ctx, const char* accessor, uint64_t entries, idl_mask_t mask);
static int determine_byte_width(idl_mask_t typespec);
static const char* determine_cast(idl_mask_t mask);
static idl_retcode_t add_alignment(context_t* ctx, int bytewidth);
static idl_retcode_t add_null(context_t* ctx, int nbytes);
static idl_retcode_t process_member(context_t* ctx, idl_member_t* mem);
static idl_retcode_t process_module(context_t* ctx, idl_module_t* module);
static idl_retcode_t process_constructed(context_t* ctx, idl_node_t* node);
static idl_retcode_t process_case(context_t* ctx, idl_case_t* _case);
static idl_retcode_t process_case_label(context_t* ctx, idl_case_label_t* label);
static idl_retcode_t write_instance_funcs(context_t* ctx, const char* accessor, uint64_t entries);
static context_t* create_context(idl_streamer_output_t* str, const char* name);
static context_t* child_context(context_t* ctx, const char* name);
static void flush_streams(context_t* ctx);
static void close_context(context_t* ctx);
static void resolve_namespace(idl_node_t* node, char** up);

static char* generatealignment(int alignto)
{
  char* returnval = NULL;
  if (alignto < 2)
  {
    returnval = idl_strdup("0;");
  }
  else if (alignto == 2)
  {
    returnval = idl_strdup("position&0x1;");
  }
  else
  {
    int mask = 0xFFFFFF;
    while (mask != 0)
    {
      if (alignto == mask + 1)
      {
        (void)idl_asprintf(&returnval, primitive_calc_alignment_shift_fmt, alignto, mask, mask);
        return returnval;
      }
      mask >>= 1;
    }

    (void)idl_asprintf(&returnval, primitive_calc_alignment_modulo_fmt, alignto, alignto, alignto);
  }
  return returnval;
}

int determine_byte_width(idl_mask_t mask)
{
  if ((mask & IDL_ENUM) == IDL_ENUM)
    mask = IDL_UINT32;

  switch (mask % (IDL_BASE_TYPE*2))
  {
  case IDL_INT8:
  case IDL_UINT8:
  case IDL_CHAR:
  case IDL_BOOL:
  case IDL_OCTET:
    return 1;
  case IDL_INT16: // is equal to IDL_SHORT
  case IDL_UINT16: // is equal to IDL_USHORT
    return 2;
  case IDL_INT32: //is equal to IDL_LONG
  case IDL_UINT32: //is equal to IDL_ULONG
  case IDL_FLOAT:
    return 4;
  case IDL_INT64: //is equal to IDL_LLONG
  case IDL_UINT64: //is equal to IDL_ULLONG
  case IDL_DOUBLE:
    return 8;
  case IDL_LDOUBLE:
    return sizeof(long double);
  }

  return -1;
}

idl_streamer_output_t* create_idl_streamer_output()
{
  idl_streamer_output_t* ptr = calloc(sizeof(idl_streamer_output_t),1);
  if (NULL != ptr)
  {
    ptr->impl_stream = create_idl_ostream(NULL);
    ptr->head_stream = create_idl_ostream(NULL);
  }
  return ptr;
}

void destruct_idl_streamer_output(idl_streamer_output_t* str)
{
  if (NULL == str)
    return;

  if (str->impl_stream != NULL)
  {
    destruct_idl_ostream(str->impl_stream);
    destruct_idl_ostream(str->head_stream);
  }
  free(str);
}

idl_ostream_t* get_idl_streamer_impl_buf(const idl_streamer_output_t* str)
{
  return str->impl_stream;
}

idl_ostream_t* get_idl_streamer_head_buf(const idl_streamer_output_t* str)
{
  return str->head_stream;
}

context_t* create_context(idl_streamer_output_t* str, const char* name)
{
  context_t* ptr = calloc(sizeof(context_t),1);
  if (NULL != ptr)
  {
    ptr->str = str;
    ptr->context = idl_strdup(name);
    ptr->currentalignment = -1;
    ptr->write_size_stream = create_idl_ostream(NULL);
    ptr->write_stream = create_idl_ostream(NULL);
    ptr->read_stream = create_idl_ostream(NULL);
  }
  return ptr;
}

context_t* child_context(context_t* ctx, const char* name)
{
  context_t *ptr = create_context(ctx->str, name);

  if (NULL != ptr)
  {
    ptr->parent = ctx;
    ptr->depth = ctx->depth + 1;
  }

  return ptr;
}

void flush_streams(context_t* ctx)
{
  transfer_ostream_buffer(ctx->write_stream, ctx->str->impl_stream);
  transfer_ostream_buffer(ctx->write_size_stream, ctx->str->impl_stream);
  transfer_ostream_buffer(ctx->read_stream, ctx->str->impl_stream);
}

void close_context(context_t* ctx)
{
  flush_streams(ctx);

  destruct_idl_ostream(ctx->write_stream);
  destruct_idl_ostream(ctx->write_size_stream);
  destruct_idl_ostream(ctx->read_stream);

  free(ctx->context);
  free(ctx);
}

void resolve_namespace(idl_node_t* node, char** up)
{
  if (!node)
    return;

  if (idl_is_module(node))
  {
    idl_module_t* mod = (idl_module_t*)node;
    if (*up)
    {
      char *temp = NULL;
      idl_asprintf(&temp, "%s::%s", idl_identifier(mod), *up);
      free(*up);
      *up = temp;
    }
    else
    {
      idl_asprintf(up, "%s::", idl_identifier(mod));
    }
  }

  resolve_namespace(node->parent, up);
}

idl_retcode_t process_node(context_t* ctx, idl_node_t* node)
{
  if (idl_is_module(node))
    process_module(ctx, (idl_module_t*)node);
  else if (idl_is_struct(node) || idl_is_union(node))
    process_constructed(ctx, node);
  else if (idl_is_typedef(node))
    process_typedef_definition(ctx, (idl_typedef_t*)node);

  if (node->next)
    process_node(ctx, node->next);

  return IDL_RETCODE_OK;
}

idl_retcode_t process_member(context_t* ctx, idl_member_t* mem)
{
  assert(ctx);
  assert(mem);

  process_instance(ctx, mem->declarators, mem->type_spec);

  if (mem->node.next)
    process_member(ctx, (idl_member_t*)(mem->node.next));

  return IDL_RETCODE_OK;
}

idl_retcode_t process_instance(context_t* ctx, idl_declarator_t* decl, idl_type_spec_t* spec)
{
  if (idl_is_base_type(spec) || idl_is_enum(spec)) {
    return process_base(ctx, decl, spec);
  } else if (idl_is_struct(spec)) {
    return process_struct(ctx, decl, (idl_struct_t*)spec);
  } else if (idl_is_templ_type(spec)) {
    // FIXME: this probably needs to loop to find the correct declarator?
    return process_template(ctx, decl, spec);
  } else {
    assert(idl_is_typedef(spec));
    return process_typedef_instance(ctx, decl, spec);
  }
}

uint64_t array_entries(idl_declarator_t* decl)
{
  if (NULL == decl)
    return 0;

  idl_const_expr_t* ce = decl->const_expr;
  uint64_t entries = 0;
  while (ce)
  {
    if ((ce->mask & IDL_CONST) == IDL_CONST)
    {
      idl_constval_t* var = (idl_constval_t*)ce;
      idl_mask_t mask = var->node.mask;
      if ((mask & IDL_UINT8) == IDL_UINT8)
      {
        if (entries)
          entries *= var->value.oct;
        else
          entries = var->value.oct;
      }
      else if ((mask & IDL_UINT32) == IDL_UINT32)
      {
        if (entries)
          entries *= var->value.ulng;
        else
          entries = var->value.ulng;
      }
      else if ((mask & IDL_UINT64) == IDL_UINT64)
      {
        if (entries)
          entries *= var->value.ullng;
        else
          entries = var->value.ullng;
      }
    }

    ce = ce->next;
  }
  return entries;
}

idl_retcode_t process_struct(context_t* ctx, idl_declarator_t* decl, idl_struct_t* spec)
{
  assert(ctx);
  assert(decl);

  uint64_t entries = array_entries(decl);

  char* accessor = NULL;
  if (decl)
  {
    char* cpp11name = get_cpp11_name(idl_identifier(decl));

    idl_asprintf(&accessor, member_access_fmt, cpp11name);
    free(cpp11name);
  }
  else
  {
    accessor = idl_strdup("obj");
  }

  write_instance_funcs(ctx, accessor, entries);
  free(accessor);

  if (NULL != decl &&
      ((idl_node_t*)decl)->next)
    process_struct(ctx, (idl_declarator_t*)((idl_node_t*)decl)->next, spec);

  return IDL_RETCODE_OK;
}

idl_retcode_t write_instance_funcs(context_t* ctx, const char* accessor, uint64_t entries)
{
  if (entries)
  {
    format_write_stream(1, ctx, instance_write_array_fmt, entries, accessor);
    format_read_stream(1, ctx, instance_read_array_fmt, entries, accessor);
    format_write_size_stream(1, ctx, instance_size_array_calc_fmt, entries, accessor);
  }
  else
  {
    format_write_stream(1, ctx, instance_write_func_fmt, accessor);
    format_read_stream(1, ctx, instance_read_func_fmt, accessor);
    format_write_size_stream(1, ctx, instance_size_func_calc_fmt, accessor);
  }

  ctx->accumulatedalignment = 0;
  ctx->currentalignment = -1;

  return IDL_RETCODE_OK;
}

idl_retcode_t add_alignment(context_t* ctx, int bytewidth)
{
  assert(ctx);

  if ((0 > ctx->currentalignment || bytewidth > ctx->currentalignment) && bytewidth != 1)
  {
    if (0 == ctx->alignmentpresent)
    {
      format_write_stream(1, ctx, "  size_t alignmentbytes = ");
      ctx->alignmentpresent = 1;
    }
    else
    {
      format_write_stream(1, ctx, "  alignmentbytes = ");
    }

    char* buffer = generatealignment(bytewidth);
    format_write_stream(0, ctx, buffer);
    format_write_stream(0, ctx, align_comment);
    format_write_stream(1, ctx, primitive_write_func_alignment_fmt);
    format_write_stream(1, ctx, primitive_incr_alignment_fmt);
    format_write_stream(0, ctx, incr_comment);

    format_read_stream(1, ctx, primitive_incr_fmt);
    format_read_stream(0, ctx, buffer);
    format_read_stream(0, ctx, align_comment);

    format_write_size_stream(1, ctx, primitive_incr_fmt);
    format_write_size_stream(0, ctx, buffer);
    format_write_size_stream(0, ctx, align_comment);

    ctx->accumulatedalignment = 0;
    ctx->currentalignment = bytewidth;

    if (buffer)
      free(buffer);
  }
  else
  {
    int missingbytes = (bytewidth - (ctx->accumulatedalignment % bytewidth)) % bytewidth;
    if (0 != missingbytes)
    {
      add_null(ctx, missingbytes);
      ctx->accumulatedalignment = 0;
    }
  }

  return IDL_RETCODE_OK;
}

idl_retcode_t add_null(context_t* ctx, int nbytes)
{
  format_write_stream(1, ctx, primitive_write_func_padding_fmt, nbytes);
  format_write_stream(1, ctx, primitive_incr_pos, nbytes);
  format_write_stream(0, ctx, incr_comment);
  format_write_size_stream(1, ctx, primitive_incr_pos, nbytes);
  format_write_size_stream(0, ctx, padding_comment);
  format_read_stream(1, ctx, primitive_incr_pos, nbytes);
  format_read_stream(0, ctx, padding_comment);

  return IDL_RETCODE_OK;
}

const char* determine_cast(idl_mask_t mask)
{
  mask %= IDL_BASE_TYPE * 2;
  switch (mask)
  {
  case IDL_CHAR:
    return char_cast;
    break;
  case IDL_BOOL:
    return bool_cast;
    break;
  case IDL_INT8:
    return int8_cast;
    break;
  case IDL_UINT8:
  case IDL_OCTET:
    return uint8_cast;
    break;
  case IDL_INT16:
    //case IDL_SHORT:
    return int16_cast;
    break;
  case IDL_UINT16:
    //case IDL_USHORT:
    return uint16_cast;
    break;
  case IDL_INT32:
    //case IDL_LONG:
    return int32_cast;
    break;
  case IDL_UINT32:
    //case IDL_ULONG:
    return uint32_cast;
    break;
  case IDL_INT64:
    //case IDL_LLONG:
    return int64_cast;
    break;
  case IDL_UINT64:
    //case IDL_ULLONG:
    return uint64_cast;
    break;
  case IDL_FLOAT:
    return float_cast;
    break;
  case IDL_DOUBLE:
    return double_cast;
    break;
  case IDL_LDOUBLE:
    return ldouble_cast;
  }
  return NULL;
}

idl_retcode_t process_known_width(context_t* ctx, const char* accessor, idl_mask_t mask, int sequence, const char *seqsizeappend)
{
  assert(ctx);
  assert(accessor);

  if ((mask & IDL_ENUM) == IDL_ENUM)
    mask = IDL_UINT32;

  const char* cast_fmt = determine_cast(mask);
  assert(cast_fmt);

  int bytewidth = determine_byte_width(mask);
  assert(bytewidth != -1);

  if (ctx->currentalignment != bytewidth)
    add_alignment(ctx, bytewidth);

  ctx->accumulatedalignment += bytewidth;

  if (0 == sequence)
  {
    format_read_stream(1, ctx, primitive_read_func_read_fmt, accessor, cast_fmt, accessor);
    format_write_stream(1, ctx, primitive_write_func_write_fmt, cast_fmt, accessor, accessor);
  }
  else
  {
    format_read_stream(1, ctx, "  ");
    format_write_stream(1, ctx, "  ");
    if (0 == ctx->sequenceentriespresent)
    {
      format_read_stream(0, ctx, "uint32_t ");
      format_write_stream(0, ctx, "uint32_t ");
      ctx->sequenceentriespresent = 1;
    }
    format_read_stream(0, ctx, primitive_read_func_seq_fmt, cast_fmt);
    format_write_stream(0, ctx, primitive_write_func_seq_fmt, accessor, seqsizeappend);
    format_write_stream(1, ctx, primitive_write_func_seq2_fmt, accessor);
  }

  format_write_size_stream(1, ctx, primitive_incr_pos, bytewidth);
  format_write_size_stream(0, ctx, "  //bytes for member: %s\n", accessor);

  format_write_stream(1, ctx, primitive_incr_pos, bytewidth);
  format_write_stream(0, ctx, incr_comment);

  format_read_stream(1, ctx, primitive_incr_pos, bytewidth);
  format_read_stream(0, ctx, incr_comment);

  return IDL_RETCODE_OK;
}

idl_retcode_t process_known_width_array(context_t* ctx, const char *accessor, uint64_t entries, idl_mask_t mask)
{
  assert(ctx);

  if ((mask & IDL_ENUM) == IDL_ENUM)
    mask = IDL_UINT32;

  int bytewidth = determine_byte_width(mask);
  assert(bytewidth != -1);

  unsigned int bw = (unsigned int)bytewidth;

  size_t bytesinarray = bw * entries;

  if (ctx->currentalignment != bytewidth)
    add_alignment(ctx, bytewidth);
  ctx->accumulatedalignment += (int)bytesinarray;

  format_write_stream(1, ctx, primitive_write_func_array_fmt, accessor, bytesinarray, accessor);
  format_read_stream(1, ctx, primitive_read_func_array_fmt, accessor, bytesinarray, accessor);

  format_write_size_stream(1, ctx, primitive_incr_pos, bytesinarray);
  format_write_size_stream(0, ctx, "  //bytes for member: %s\n", accessor);

  format_write_stream(1, ctx, primitive_incr_pos, bytesinarray);
  format_write_stream(0, ctx, incr_comment);

  format_read_stream(1, ctx, primitive_incr_pos, bytesinarray);
  format_read_stream(0, ctx, incr_comment);

  return IDL_RETCODE_OK;
}

idl_retcode_t process_template(context_t* ctx, idl_declarator_t* decl, idl_type_spec_t* tspec)
{
  assert(ctx);
  assert(tspec);

  uint64_t entries = array_entries(decl);

  int oldal = ctx->alignmentpresent;
  int oldep = ctx->sequenceentriespresent;
  if (entries)
  {
    format_write_size_stream(1,ctx, array_for_loop,entries);
    format_write_stream(1, ctx, array_for_loop, entries);
    format_read_stream(1, ctx, array_for_loop, entries);

    ctx->depth++;
  }

  char* accessor = NULL;
  if (decl)
  {
    char* cpp11name = get_cpp11_name(idl_identifier(decl));
    idl_asprintf(&accessor, member_access_fmt, cpp11name);
    free(cpp11name);
  }
  else
  {
    accessor = idl_strdup("obj");
  }

  if (idl_is_sequence(tspec) ||
      idl_is_string(tspec))
  {
    size_t bound = 0;
    idl_type_spec_t* ispec = tspec;
    if (idl_is_sequence(tspec))
    {
      //change member_mask to the type of the sequence template
      bound = ((idl_sequence_t*)tspec)->maximum;
      ispec = ((idl_sequence_t*)tspec)->type_spec;
    }
    else if (idl_is_string(tspec))
    {
      bound = ((idl_string_t*)tspec)->maximum;
    }

    if (idl_is_typedef(ispec))
    {
      idl_type_spec_t* temp = resolve_typedef(ispec);
      if (idl_is_base_type(temp))
        ispec = temp;
    }

    char* buffer;
    idl_asprintf(&buffer, seq_size_fmt, accessor);
    process_known_width(ctx, buffer, IDL_UINT32, 1, idl_is_string(ispec) ? "+1":"");

    if (bound)
    {
      //add boundary checking function
      format_write_stream(1, ctx, sequence_length_exception_fmt, bound, accessor, bound);
    }

    if (buffer)
      free(buffer);

    if (ispec->mask == IDL_WCHAR)
    {
      fprintf(stderr, "wchar sequences are currently not supported\n");
    }
    else if (idl_is_base_type(ispec) ||
             idl_is_string(tspec))
    {
      int bytewidth = 1;

      const char* cast_fmt = char_cast;
      if (idl_is_base_type(ispec))
      {
        cast_fmt = determine_cast(ispec->mask);
        bytewidth = determine_byte_width(ispec->mask);  //determine byte width of base type
      }
      if (bytewidth > 4)
        add_alignment(ctx, bytewidth);

      format_write_stream(1, ctx, seq_primitive_write_fmt, accessor, bytewidth, accessor);
      format_read_stream(1, ctx, seq_primitive_read_fmt, accessor, cast_fmt, cast_fmt);
      format_write_stream(1, ctx, seq_incr_fmt, bytewidth);
      format_write_stream(0, ctx, incr_comment);
      format_write_size_stream(1, ctx, seq_entries_fmt, accessor, idl_is_string(tspec) ? "+1" : "", bytewidth);
      format_read_stream(1, ctx, seq_incr_fmt, bytewidth);
      format_read_stream(0, ctx, incr_comment);
    }
    else
    {
      if (idl_is_typedef(ispec))
      {
        char* ns = idl_strdup("");
        idl_typedef_t* td = ((idl_typedef_t*)ispec);
        resolve_namespace(td->type_spec, &ns);
        format_write_stream(1, ctx, seq_typedef_write_fmt, accessor, ns, idl_identifier(td->declarators));
        format_write_size_stream(1, ctx, seq_typedef_write_size_fmt, accessor, ns, idl_identifier(td->declarators));
        format_read_stream(1, ctx, seq_read_resize_fmt, accessor);
        format_read_stream(1, ctx, seq_typedef_read_copy_fmt, ns, idl_identifier(td->declarators), accessor);
        free(ns);
      }
      else
      {
        format_write_stream(1, ctx, seq_structured_write_fmt, accessor);
        format_write_size_stream(1, ctx, seq_structured_write_size_fmt, accessor);
        format_read_stream(1, ctx, seq_read_resize_fmt, accessor);
        format_read_stream(1, ctx, seq_structured_read_copy_fmt, accessor);
      }
    }

    ctx->accumulatedalignment = 0;
    ctx->currentalignment = -1;
  }
  else if (tspec->mask == IDL_WSTRING)
  {
    fprintf(stderr, "wstring types are currently not supported\n");
  }
  else if (tspec->mask == IDL_FIXED_PT)
  {
    fprintf(stderr, "fixed point types are currently not supported\n");
#if 0
    //fputs("fixed point type template classes not supported at this time", stderr);

    format_write_stream(1, ctx, "  {\n");
    format_write_stream(1, ctx, fixed_pt_write_digits, cpp11name, cpp11name);
    format_write_stream(1, ctx, fixed_pt_write_byte, cpp11name);

    for (size_t i = 0; i < sizeof(fixed_pt_write_fill) / sizeof(const char*); i++)
    {
      format_write_stream(1, ctx, fixed_pt_write_fill[i]);
    }
    format_write_stream(1, ctx, fixed_pt_write_position, cpp11name);
    format_write_size_stream(1, ctx, "  ");
    format_write_size_stream(0, ctx, fixed_pt_write_position, cpp11name);
    format_write_stream(1, ctx, "  }\n");
    format_read_stream(1, ctx, "  {\n");
    format_read_stream(1, ctx, fixed_pt_read_byte, cpp11name);

    for (size_t i = 0; i < sizeof(fixed_pt_read_fill) / sizeof(const char*); i++)
    {
      format_read_stream(1, ctx, fixed_pt_read_fill[i]);
    }

    format_read_stream(1, ctx, fixed_pt_read_assign, cpp11name, cpp11name);
    format_read_stream(1, ctx, fixed_pt_read_position, cpp11name);
    format_read_stream(1, ctx, "  }\n");

    ctx->accumulatedalignment = 0;
    ctx->currentalignment = -1;
#endif
  }

  if (entries)
  {
    ctx->depth--;

    ctx->alignmentpresent = oldal;
    ctx->sequenceentriespresent = oldep;

    format_write_size_stream(1, ctx, "  }\n");
    format_write_stream(1, ctx, "  }\n");
    format_read_stream(1, ctx, "  }\n");
  }

  if (accessor)
    free(accessor);

  if (NULL != decl &&
    ((idl_node_t*)decl)->next)
    process_template(ctx, (idl_declarator_t*)((idl_node_t*)decl)->next, tspec);

  return IDL_RETCODE_OK;
}

idl_retcode_t process_module(context_t* ctx, idl_module_t* module)
{
  assert(ctx);
  assert(module);

  if (module->definitions)
  {
    char* cpp11name = get_cpp11_name(idl_identifier(module));
    assert(cpp11name);

    context_t* newctx = child_context(ctx, cpp11name);

    format_write_stream(1, ctx, namespace_declaration_fmt, cpp11name);
    format_write_stream(1, ctx, "{\n\n");

    format_header_stream(1, ctx, namespace_declaration_fmt, cpp11name);
    format_header_stream(1, ctx, "{\n\n");

    flush_streams(ctx);

    process_node(newctx, (idl_node_t*)module->definitions);

    close_context(newctx);
    format_read_stream(1, ctx, namespace_closure_fmt, cpp11name);
    format_header_stream(1, ctx, namespace_closure_fmt, cpp11name);

    flush_streams(ctx);

    free(cpp11name);
  }

  return IDL_RETCODE_OK;
}

idl_retcode_t process_constructed(context_t* ctx, idl_node_t* node)
{
  assert(ctx);
  assert(node);

  char* cpp11name = NULL;

  if (idl_is_struct(node) ||
      idl_is_union(node))
  {
    if (idl_is_struct(node))
      cpp11name = get_cpp11_name(idl_identifier((idl_struct_t*)node));
    else if (idl_is_union(node))
      cpp11name = get_cpp11_name(idl_identifier((idl_union_t*)node));
    assert(cpp11name);

    format_write_stream(1, ctx, struct_write_func_fmt, cpp11name);
    format_write_stream(0, ctx, "\n");
    format_write_stream(1, ctx, "{\n");

    format_write_size_stream(1, ctx, struct_write_size_func_fmt, cpp11name);
    format_write_size_stream(0, ctx, "\n");
    format_write_size_stream(1, ctx, "{\n");
    format_write_size_stream(1, ctx, "  size_t position = offset;\n");

    format_read_stream(1, ctx, struct_read_func_fmt, cpp11name);
    format_read_stream(0, ctx, "\n");
    format_read_stream(1, ctx, "{\n");

    ctx->currentalignment = -1;
    ctx->alignmentpresent = 0;
    ctx->sequenceentriespresent = 0;
    ctx->accumulatedalignment = 0;

    if (idl_is_struct(node))
    {
      idl_struct_t* _struct = (idl_struct_t*)node;
      if (_struct->base_type)
      {
        char* base_cpp11name = get_cpp11_name(idl_identifier(_struct->base_type));
        char* ns = idl_strdup("");
        assert(base_cpp11name);
        resolve_namespace((idl_node_t*)_struct->base_type, &ns);
        char* accessor = NULL;
        if (idl_asprintf(&accessor, ref_cast_fmt, ns, base_cpp11name) == -1)
          return IDL_RETCODE_NO_MEMORY;
        write_instance_funcs(ctx, accessor, 0);
        free(base_cpp11name);
        free(accessor);
        free(ns);
      }

      if (_struct->members)
        process_member(ctx, _struct->members);
    }
    else if (idl_is_union(node))
    {
      idl_union_t* _union = (idl_union_t*)node;
      idl_switch_type_spec_t* st = _union->switch_type_spec;

      idl_mask_t disc_mask = st->mask;
      if (idl_is_enumerator(st)) {
        disc_mask = IDL_ULONG;
      } else {
        assert(idl_is_masked(st, IDL_BASE_TYPE));
      }

      format_read_stream(1, ctx, union_clear_func);
      process_known_width(ctx, "_d()", disc_mask, 0, "");
      format_write_size_stream(1, ctx, union_switch_fmt);
      format_write_size_stream(1, ctx, "  {\n");
      format_write_stream(1, ctx, union_switch_fmt);
      format_write_stream(1, ctx, "  {\n");
      format_read_stream(1, ctx, union_switch_fmt);
      format_read_stream(1, ctx, "  {\n");

      if (_union->cases)
      {
        ctx->depth++;
        process_case(ctx, _union->cases);
        ctx->depth--;
      }

      ctx->currentalignment = -1;
      ctx->accumulatedalignment = 0;
      ctx->alignmentpresent = 0;

      format_write_stream(1, ctx, "  }\n");
      format_write_size_stream(1, ctx, "  }\n");
      format_read_stream(1, ctx, "  }\n");
    }

    format_write_size_stream(1, ctx, "  return position-offset;\n");
    format_write_size_stream(1, ctx, "}\n\n");
    format_write_stream(1, ctx, "  return position;\n");
    format_write_stream(1, ctx, "}\n\n");
    format_read_stream(1, ctx, "  return position;\n");
    format_read_stream(1, ctx, "}\n\n");
  }

  if (cpp11name)
    free(cpp11name);
  return IDL_RETCODE_OK;
}

idl_retcode_t process_case(context_t* ctx, idl_case_t* _case)
{
  if (_case->case_labels)
    process_case_label(ctx, _case->case_labels);

  format_write_stream(1, ctx, "  {\n");
  format_write_size_stream(1, ctx, "  {\n");
  format_read_stream(1, ctx, "  {\n");
  ctx->depth++;

  process_instance(ctx, _case->declarator, _case->type_spec);

  ctx->depth--;
  format_write_stream(1, ctx, "  }\n");
  format_write_stream(1, ctx, union_case_ending);
  format_write_size_stream(1, ctx, "  }\n");
  format_write_size_stream(1, ctx, union_case_ending);
  format_read_stream(1, ctx, "  }\n");
  format_read_stream(1, ctx, union_case_ending);

  //reset alignment to 4
  ctx->currentalignment = 4;
  ctx->accumulatedalignment = 0;
  ctx->alignmentpresent = 1;

  //go to next case
  if (_case->node.next)
    process_case(ctx, (idl_case_t*)_case->node.next);
  return IDL_RETCODE_OK;
}

idl_type_spec_t* resolve_typedef(idl_type_spec_t* spec)
{
  while (NULL != spec &&
         idl_is_typedef(spec))
    spec = ((idl_typedef_t*)spec)->type_spec;

  return spec;
}

idl_retcode_t process_typedef_definition(context_t* ctx, idl_typedef_t* node)
{
  idl_type_spec_t* spec = resolve_typedef(node->type_spec);
  if (idl_is_base_type(spec) ||
      idl_is_enum(spec))
    return IDL_RETCODE_OK;
  else
  {
    const char* tsname = idl_identifier(node->declarators);

    format_write_stream(1, ctx, typedef_write_func_fmt, tsname, tsname);
    format_write_stream(0, ctx, "\n");
    format_write_stream(1, ctx, "{\n");
    format_write_size_stream(1, ctx, typedef_write_size_func_fmt, tsname, tsname);
    format_write_size_stream(0, ctx, "\n");
    format_write_size_stream(1, ctx, "{\n");
    format_read_stream(1, ctx, typedef_read_func_fmt, tsname, tsname);
    format_read_stream(0, ctx, "\n");
    format_read_stream(1, ctx, "{\n");

    format_header_stream(1, ctx, typedef_write_func_fmt, tsname, tsname);
    format_header_stream(0, ctx, ";\n\n");
    format_header_stream(1, ctx, typedef_write_size_func_fmt, tsname, tsname);
    format_header_stream(0, ctx, ";\n\n");
    format_header_stream(1, ctx, typedef_read_func_fmt, tsname, tsname);
    format_header_stream(0, ctx, ";\n\n");

    process_instance(ctx, NULL, spec);

    format_write_stream(1, ctx,"}\n\n");
    format_write_size_stream(1, ctx, "}\n\n");
    format_read_stream(1, ctx, "}\n\n");
  }
  return IDL_RETCODE_OK;
}

idl_retcode_t process_typedef_instance(context_t* ctx, idl_declarator_t* decl, idl_type_spec_t* spec)
{
  idl_type_spec_t* ispec = resolve_typedef(spec);
  if (idl_is_base_type(ispec))
    process_base(ctx, decl, ispec);
  else
    process_instance(ctx, decl, ispec);

  if (NULL != decl &&
    ((idl_node_t*)decl)->next)
    process_typedef_instance(ctx, (idl_declarator_t*)((idl_node_t*)decl)->next, spec);

  return IDL_RETCODE_OK;
}

idl_retcode_t process_case_label(context_t* ctx, idl_case_label_t* label)
{
  idl_const_expr_t* ce = label->const_expr;
  if (ce)
  {
    char* buffer = NULL;
    idl_literal_t* lit = (idl_literal_t*)ce;
    if (idl_is_masked(ce, IDL_INTEGER_LITERAL))
    {
      assert(idl_is_masked(ce, IDL_INTEGER_LITERAL));
      if (idl_asprintf(&buffer, "%lu", lit->value.ullng) == -1)
        return IDL_RETCODE_NO_MEMORY;
    }
    else if (idl_is_masked(ce, IDL_BOOLEAN_LITERAL))
    {
      if (!(buffer = idl_strdup(lit->value.bln ? "true" : "false")))
        return IDL_RETCODE_NO_MEMORY;
    }
    else if (idl_is_masked(ce, IDL_CHAR_LITERAL))
    {
      if (idl_asprintf(&buffer, "\'%s\'", lit->value.str) == -1)
        return IDL_RETCODE_NO_MEMORY;
    }

    if (buffer)
    {
      format_write_stream(1, ctx, union_case_fmt, buffer);
      format_write_size_stream(1, ctx, union_case_fmt, buffer);
      format_read_stream(1, ctx, union_case_fmt, buffer);
      free(buffer);
    }
  }
  else
  {
    add_default_case(ctx);
  }

  if (label->node.next)
    process_case_label(ctx, (idl_case_label_t*)label->node.next);

  return IDL_RETCODE_OK;
}

idl_retcode_t add_default_case(context_t* ctx)
{
  format_write_stream(1, ctx, default_case_fmt);
  format_write_size_stream(1, ctx, default_case_fmt);
  format_read_stream(1, ctx, default_case_fmt);

  return IDL_RETCODE_OK;
}

idl_retcode_t process_base(context_t* ctx, idl_declarator_t* decl, idl_type_spec_t* tspec)
{
  assert(ctx);
  assert(tspec);

  uint64_t entries = array_entries(decl);
  char* accessor = NULL;
  if (decl)
  {
    char* cpp11name = get_cpp11_name(idl_identifier(decl));
    idl_asprintf(&accessor, member_access_fmt, cpp11name);
    free(cpp11name);
  }
  else
  {
    accessor = idl_strdup("obj");
  }
  assert(accessor);

  if (entries)
    process_known_width_array(ctx, accessor, entries, tspec->mask);
  else
    process_known_width(ctx, accessor, tspec->mask, 0, "");

  if (accessor)
    free(accessor);

  if (NULL != decl &&
      ((idl_node_t*)decl)->next)
    process_base(ctx, (idl_declarator_t*)((idl_node_t*)decl)->next, tspec);

  return IDL_RETCODE_OK;
}

void idl_streamers_generate(const idl_tree_t* tree, idl_streamer_output_t* str)
{
  context_t* ctx = create_context(str, "");
  process_node(ctx, tree->root);
  close_context(ctx);
}
