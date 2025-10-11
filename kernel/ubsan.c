// #include <cpu/arch_inst.h>
// #include <interrupts.h>
// #include <kubsan.h>
#include <kprintf.h>
#include <log.h>
// #include <stdio.h>

enum { type_kind_int = 0, type_kind_float = 1, type_unknown = 0xffff };

struct type_descriptor {
  u16 type_kind;
  u16 type_info;
  char type_name[1];
};

struct source_location {
  const char *file_name;
  union {
    unsigned long reported;
    struct {
      u32 line;
      u32 column;
    };
  };
};

struct OverflowData {
  struct source_location location;
  struct type_descriptor *type;
};

struct type_mismatch_data {
  struct source_location location;
  struct type_descriptor *type;
  unsigned long alignment;
  unsigned char type_check_kind;
};

struct type_mismatch_data_v1 {
  struct source_location location;
  struct type_descriptor *type;
  unsigned char log_alignment;
  unsigned char type_check_kind;
};

struct type_mismatch_data_common {
  struct source_location *location;
  struct type_descriptor *type;
  unsigned long alignment;
  unsigned char type_check_kind;
};

struct nonnull_arg_data {
  struct source_location location;
  struct source_location attr_location;
  int arg_index;
};

struct OutOfBoundsData {
  struct source_location location;
  struct type_descriptor *array_type;
  struct type_descriptor *index_type;
};

struct ShiftOutOfBoundsData {
  struct source_location location;
  struct type_descriptor *lhs_type;
  struct type_descriptor *rhs_type;
};

struct unreachable_data {
  struct source_location location;
};

struct invalid_value_data {
  struct source_location location;
  struct type_descriptor *type;
};

struct alignment_assumption_data {
  struct source_location location;
  struct source_location assumption_location;
  struct type_descriptor *type;
};

void ubsan_log(const char *cause, struct source_location source) {
  kprintf("%s: %s : %d\n", cause, source.file_name, source.line);
  for (;;)
    ;
  //  dump_backtrace(5);
  //  disable_interrupts();
  //  halt();
}

void __ubsan_handle_shift_out_of_bounds(struct ShiftOutOfBoundsData *data,
                                        unsigned long lhs, unsigned long rhs) {
  (void)lhs;
  (void)rhs;
  ubsan_log("handle_shift_out_of_bounds", data->location);
}

void __ubsan_handle_add_overflow(struct OverflowData *data, unsigned long lhs,
                                 unsigned long rhs) {
  (void)lhs;
  (void)rhs;
  ubsan_log("handle_add_overflow", data->location);
}

void __ubsan_handle_sub_overflow(struct OverflowData *data, unsigned long lhs,
                                 unsigned long rhs) {
  (void)lhs;
  (void)rhs;
  ubsan_log("handle_sub_overflow", data->location);
}

void __ubsan_handle_mul_overflow(struct OverflowData *data, unsigned long lhs,
                                 unsigned long rhs) {
  (void)lhs;
  (void)rhs;
  ubsan_log("handle_mul_overflow", data->location);
}

void __ubsan_handle_negate_overflow(struct OverflowData *data,
                                    unsigned long lhs, unsigned long rhs) {
  (void)lhs;
  (void)rhs;
  ubsan_log("handle_negate_overflow", data->location);
}

void __ubsan_handle_divrem_overflow(struct OverflowData *data,
                                    unsigned long lhs, unsigned long rhs) {
  (void)lhs;
  (void)rhs;
  ubsan_log("handle_divrem_overflow", data->location);
}

void __ubsan_handle_out_of_bounds(struct OutOfBoundsData *data, void *index) {
  (void)index;
  ubsan_log("handle_out_of_bounds", data->location);
}

void __ubsan_handle_pointer_overflow(struct OutOfBoundsData *data,
                                     void *index) {
  (void)index;
  ubsan_log("handle_pointer_overflow", data->location);
}

void __ubsan_handle_vla_bound_not_positive(struct OutOfBoundsData *data,
                                           void *index) {
  (void)index;
  ubsan_log("handle_vla_bound_not_positive", data->location);
}
