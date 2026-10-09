/*
 * The subset of a Win9x INF that V9XUPD.EXE applies itself. See
 * update_inf.h for what the subset is and why it is that and no more.
 *
 * No C library: this links into V9XUPD.EXE, which has no runtime.
 */
#include "velocity9x/update_inf.h"
#include "velocity9x/update_proto.h"

/* LDID 11: the Windows system directory, the only destination the
 * generator uses and the only one the updater stages renames into. */
#define V9X_INF_SYSTEM_LDID "11"

/* AddReg is root,subkey,name,flags,value; DelReg is root,subkey[,name]. */
#define V9X_INF_FIELDS_MAX 5u
#define V9X_INF_FIELD_MAX  V9X_INF_DATA_MAX

#define V9X_INF_SECTIONS_MAX 16u
#define V9X_INF_SECTION_NAME_MAX 40u

struct v9x_inf_line {
    const char *start;
    v9x_u32 length;
};

struct v9x_inf_lists {
    char copy[V9X_INF_SECTIONS_MAX][V9X_INF_SECTION_NAME_MAX];
    v9x_u32 copy_count;
    char del[V9X_INF_SECTIONS_MAX][V9X_INF_SECTION_NAME_MAX];
    v9x_u32 del_count;
    char add[V9X_INF_SECTIONS_MAX][V9X_INF_SECTION_NAME_MAX];
    v9x_u32 add_count;
};

static char v9x_inf_lower(char value)
{
    if (value >= 'A' && value <= 'Z') {
        return (char)(value - 'A' + 'a');
    }
    return value;
}

static v9x_u16 v9x_inf_span_equals(const char *left, v9x_u32 length,
                                   const char *right)
{
    v9x_u32 index;

    for (index = 0u; index < length; ++index) {
        if (right[index] == '\0' ||
            v9x_inf_lower(left[index]) != v9x_inf_lower(right[index])) {
            return V9X_FALSE;
        }
    }
    return right[length] == '\0' ? V9X_TRUE : V9X_FALSE;
}

static void v9x_inf_fail(struct v9x_inf_plan *plan, const char *reason,
                         const char *detail, v9x_u32 detail_length)
{
    v9x_u32 used = 0u;
    v9x_u32 index;

    for (index = 0u; reason[index] != '\0' &&
                     used + 1u < sizeof(plan->error); ++index) {
        plan->error[used++] = reason[index];
    }
    if (detail != 0 && used + 3u < sizeof(plan->error)) {
        plan->error[used++] = ':';
        plan->error[used++] = ' ';
        /* A line is bounded by its length, a section name by its NUL. */
        for (index = 0u; index < detail_length && detail[index] != '\0' &&
                         used + 1u < sizeof(plan->error); ++index) {
            plan->error[used++] = detail[index];
        }
    }
    plan->error[used] = '\0';
}

/*
 * The meaningful lines of [section], one per call: comments (a ';' outside
 * quotes) and surrounding blanks removed, empty lines skipped. *cursor is 0
 * to start; V9X_FALSE at the section's end or when it does not exist.
 */
static v9x_u16 v9x_inf_next_line(const char *text, v9x_u32 length,
                                 const char *section, v9x_u32 *cursor,
                                 struct v9x_inf_line *line)
{
    v9x_u32 position = *cursor;

    if (position == 0u) {
        /* Find the header. */
        v9x_u16 found = V9X_FALSE;

        while (position < length && !found) {
            v9x_u32 start = position;
            v9x_u32 end;

            while (position < length && text[position] != '\n') {
                ++position;
            }
            end = position;
            if (position < length) {
                ++position;
            }
            while (start < end && (text[start] == ' ' || text[start] == '\t')) {
                ++start;
            }
            while (end > start && (text[end - 1u] == '\r' ||
                                   text[end - 1u] == ' ' ||
                                   text[end - 1u] == '\t')) {
                --end;
            }
            if (end - start >= 2u && text[start] == '[' &&
                text[end - 1u] == ']' &&
                v9x_inf_span_equals(text + start + 1u, end - start - 2u,
                                    section)) {
                found = V9X_TRUE;
            }
        }
        if (!found) {
            return V9X_FALSE;
        }
    }

    while (position < length) {
        v9x_u32 start = position;
        v9x_u32 end;
        v9x_u32 scan;
        v9x_u16 quoted = V9X_FALSE;

        while (position < length && text[position] != '\n') {
            ++position;
        }
        end = position;
        if (position < length) {
            ++position;
        }
        for (scan = start; scan < end; ++scan) {
            if (text[scan] == '"') {
                quoted = quoted ? V9X_FALSE : V9X_TRUE;
            }
            if (text[scan] == ';' && !quoted) {
                end = scan;
                break;
            }
        }
        while (start < end && (text[start] == ' ' || text[start] == '\t')) {
            ++start;
        }
        while (end > start && (text[end - 1u] == '\r' ||
                               text[end - 1u] == ' ' ||
                               text[end - 1u] == '\t')) {
            --end;
        }
        if (start == end) {
            continue;
        }
        if (text[start] == '[') {
            /* The next section: this one is over. */
            *cursor = length;
            return V9X_FALSE;
        }
        line->start = text + start;
        line->length = end - start;
        *cursor = position;
        return V9X_TRUE;
    }
    *cursor = length;
    return V9X_FALSE;
}

/*
 * Split a line at commas outside quotes into at most V9X_INF_FIELDS_MAX
 * fields, quotes removed and blanks trimmed. Returns the field count, or 0
 * for too many fields, an over-long field, an unterminated quote, text
 * after a closing quote, or a % string substitution.
 */
static v9x_u32 v9x_inf_fields(const struct v9x_inf_line *line,
                              char fields[V9X_INF_FIELDS_MAX][V9X_INF_FIELD_MAX])
{
    v9x_u32 count = 0u;
    v9x_u32 position = 0u;

    for (;;) {
        v9x_u32 used = 0u;
        char *field;

        if (count == V9X_INF_FIELDS_MAX) {
            return 0u;
        }
        field = fields[count];
        while (position < line->length && line->start[position] == ' ') {
            ++position;
        }
        if (position < line->length && line->start[position] == '"') {
            ++position;
            while (position < line->length && line->start[position] != '"') {
                if (used + 1u >= V9X_INF_FIELD_MAX) {
                    return 0u;
                }
                field[used++] = line->start[position++];
            }
            if (position == line->length) {
                return 0u;
            }
            ++position;
            while (position < line->length && line->start[position] == ' ') {
                ++position;
            }
            if (position < line->length && line->start[position] != ',') {
                return 0u;
            }
        } else {
            while (position < line->length && line->start[position] != ',') {
                if (used + 1u >= V9X_INF_FIELD_MAX) {
                    return 0u;
                }
                field[used++] = line->start[position++];
            }
            while (used != 0u && field[used - 1u] == ' ') {
                --used;
            }
        }
        field[used] = '\0';
        {
            v9x_u32 index;

            for (index = 0u; index < used; ++index) {
                if (field[index] == '%') {
                    return 0u;
                }
            }
        }
        ++count;
        if (position == line->length) {
            return count;
        }
        ++position;
    }
}

static v9x_u16 v9x_inf_copy_text(char *target, v9x_u32 capacity,
                                 const char *source)
{
    v9x_u32 index;

    for (index = 0u; source[index] != '\0'; ++index) {
        if (index + 1u >= capacity) {
            return V9X_FALSE;
        }
        target[index] = source[index];
    }
    target[index] = '\0';
    return V9X_TRUE;
}

/* "A,B , C" into names. */
static v9x_u16 v9x_inf_section_list(const char *list, v9x_u32 length,
                                    char names[V9X_INF_SECTIONS_MAX][V9X_INF_SECTION_NAME_MAX],
                                    v9x_u32 *count)
{
    v9x_u32 position = 0u;

    while (position <= length) {
        v9x_u32 used = 0u;

        while (position < length && list[position] == ' ') {
            ++position;
        }
        if (*count == V9X_INF_SECTIONS_MAX) {
            return V9X_FALSE;
        }
        while (position < length && list[position] != ',') {
            if (used + 1u >= V9X_INF_SECTION_NAME_MAX) {
                return V9X_FALSE;
            }
            names[*count][used++] = list[position++];
        }
        while (used != 0u && names[*count][used - 1u] == ' ') {
            --used;
        }
        if (used == 0u) {
            return V9X_FALSE;
        }
        names[*count][used] = '\0';
        ++*count;
        ++position;
    }
    return V9X_TRUE;
}

static v9x_u16 v9x_inf_root(const char *text)
{
    if (v9x_inf_span_equals(text, 3u, "HKR")) {
        return V9X_INF_ROOT_HKR;
    }
    if (v9x_inf_span_equals(text, 4u, "HKLM")) {
        return V9X_INF_ROOT_HKLM;
    }
    if (v9x_inf_span_equals(text, 4u, "HKCR")) {
        return V9X_INF_ROOT_HKCR;
    }
    return V9X_INF_ROOT_NONE;
}

/* An 8.3 name the system directory can hold: base 1-8 and extension 1-3
 * of letters, digits, hyphen or underscore. */
static v9x_u16 v9x_inf_short_name(const char *name)
{
    v9x_u32 base = 0u;
    v9x_u32 extension = 0u;
    v9x_u16 dot = V9X_FALSE;

    for (; *name != '\0'; ++name) {
        char value = *name;

        if (value == '.') {
            if (dot) {
                return V9X_FALSE;
            }
            dot = V9X_TRUE;
            continue;
        }
        if (!((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '-' || value == '_')) {
            return V9X_FALSE;
        }
        if (dot) {
            ++extension;
        } else {
            ++base;
        }
    }
    return (base >= 1u && base <= 8u && dot && extension >= 1u &&
            extension <= 3u) ? V9X_TRUE : V9X_FALSE;
}

static struct v9x_inf_op *v9x_inf_new_op(struct v9x_inf_plan *plan)
{
    struct v9x_inf_op *op;

    if (plan->count >= V9X_INF_OPS_MAX) {
        return 0;
    }
    op = &plan->ops[plan->count++];
    op->kind = 0u;
    op->root = V9X_INF_ROOT_NONE;
    op->copy_flags = 0u;
    op->key[0] = '\0';
    op->name[0] = '\0';
    op->data[0] = '\0';
    return op;
}

static v9x_u16 v9x_inf_plan_copies(const char *text, v9x_u32 length,
                                   const char *section,
                                   struct v9x_inf_plan *plan)
{
    static char fields[V9X_INF_FIELDS_MAX][V9X_INF_FIELD_MAX];
    char destination[8];
    struct v9x_inf_line line;
    v9x_u32 cursor = 0u;
    v9x_u16 any = V9X_FALSE;

    /* The section's own DestinationDirs entry, else DefaultDestDir: it must
     * be the system directory. */
    if (!v9x_update_ini_value(text, length, "DestinationDirs", section,
                              destination, sizeof(destination)) &&
        !v9x_update_ini_value(text, length, "DestinationDirs",
                              "DefaultDestDir", destination,
                              sizeof(destination))) {
        v9x_inf_fail(plan, "no destination for", section,
                     (v9x_u32)sizeof(plan->error));
        return V9X_FALSE;
    }
    if (!v9x_inf_span_equals(destination, 2u, V9X_INF_SYSTEM_LDID)) {
        v9x_inf_fail(plan, "destination is not the system directory",
                     section, (v9x_u32)sizeof(plan->error));
        return V9X_FALSE;
    }

    while (v9x_inf_next_line(text, length, section, &cursor, &line)) {
        struct v9x_inf_op *op;
        v9x_u32 count = v9x_inf_fields(&line, fields);
        v9x_u32 flags = 0u;
        v9x_u32 index;

        any = V9X_TRUE;
        /* file[,rename source[,unused[,flags]]]: no rename, flags 12 or 40. */
        if (count == 0u || (count >= 2u && fields[1][0] != '\0') ||
            (count >= 3u && fields[2][0] != '\0') ||
            !v9x_inf_short_name(fields[0])) {
            v9x_inf_fail(plan, "unsupported CopyFiles line", line.start,
                         line.length);
            return V9X_FALSE;
        }
        if (count == 4u) {
            for (index = 0u; fields[3][index] != '\0'; ++index) {
                if (fields[3][index] < '0' || fields[3][index] > '9' ||
                    index > 4u) {
                    v9x_inf_fail(plan, "unsupported copy flags", line.start,
                                 line.length);
                    return V9X_FALSE;
                }
                flags = flags * 10u + (v9x_u32)(fields[3][index] - '0');
            }
        }
        if (flags != V9X_INF_COPY_ALWAYS && flags != V9X_INF_COPY_IF_NEWER) {
            v9x_inf_fail(plan, "unsupported copy flags", line.start,
                         line.length);
            return V9X_FALSE;
        }
        op = v9x_inf_new_op(plan);
        if (op == 0) {
            v9x_inf_fail(plan, "too many operations", 0, 0u);
            return V9X_FALSE;
        }
        op->kind = V9X_INF_OP_COPY;
        op->copy_flags = (v9x_u16)flags;
        (void)v9x_inf_copy_text(op->name, sizeof(op->name), fields[0]);
        ++plan->copies;
    }
    if (!any) {
        v9x_inf_fail(plan, "empty or missing section", section,
                     (v9x_u32)sizeof(plan->error));
        return V9X_FALSE;
    }
    return V9X_TRUE;
}

static v9x_u16 v9x_inf_plan_registry(const char *text, v9x_u32 length,
                                     const char *section, v9x_u16 adding,
                                     struct v9x_inf_plan *plan)
{
    static char fields[V9X_INF_FIELDS_MAX][V9X_INF_FIELD_MAX];
    struct v9x_inf_line line;
    v9x_u32 cursor = 0u;
    v9x_u16 any = V9X_FALSE;

    while (v9x_inf_next_line(text, length, section, &cursor, &line)) {
        struct v9x_inf_op *op;
        v9x_u32 count = v9x_inf_fields(&line, fields);
        v9x_u16 root;

        any = V9X_TRUE;
        root = count != 0u ? v9x_inf_root(fields[0]) : V9X_INF_ROOT_NONE;
        if (root == V9X_INF_ROOT_NONE) {
            v9x_inf_fail(plan, "unsupported registry line", line.start,
                         line.length);
            return V9X_FALSE;
        }
        op = v9x_inf_new_op(plan);
        if (op == 0) {
            v9x_inf_fail(plan, "too many operations", 0, 0u);
            return V9X_FALSE;
        }
        op->root = root;
        if (!v9x_inf_copy_text(op->key, sizeof(op->key), fields[1])) {
            v9x_inf_fail(plan, "key too long", line.start, line.length);
            return V9X_FALSE;
        }

        if (adding) {
            /* root,subkey,name,flags,value; flags empty or 0 (REG_SZ). */
            if (count != 5u ||
                !(fields[3][0] == '\0' ||
                  (fields[3][0] == '0' && fields[3][1] == '\0')) ||
                !v9x_inf_copy_text(op->name, sizeof(op->name), fields[2]) ||
                !v9x_inf_copy_text(op->data, sizeof(op->data), fields[4])) {
                v9x_inf_fail(plan, "unsupported AddReg line", line.start,
                             line.length);
                return V9X_FALSE;
            }
            op->kind = V9X_INF_OP_SET_STRING;
            continue;
        }

        /* root,subkey deletes the key; root,subkey,name one value. Never
         * the root itself: "HKR," would delete the whole driver key. */
        if (count == 2u && op->key[0] != '\0') {
            op->kind = V9X_INF_OP_DEL_KEY;
            continue;
        }
        if (count == 3u && fields[2][0] != '\0' &&
            v9x_inf_copy_text(op->name, sizeof(op->name), fields[2])) {
            op->kind = V9X_INF_OP_DEL_VALUE;
            continue;
        }
        v9x_inf_fail(plan, "unsupported DelReg line", line.start,
                     line.length);
        return V9X_FALSE;
    }
    if (!any) {
        v9x_inf_fail(plan, "empty or missing section", section,
                     (v9x_u32)sizeof(plan->error));
        return V9X_FALSE;
    }
    return V9X_TRUE;
}

/* Copy text[start..end) trimmed of blanks and one pair of quotes. */
static v9x_u16 v9x_inf_copy_span(const char *text, v9x_u32 start,
                                 v9x_u32 end, char *output, v9x_u32 capacity)
{
    v9x_u32 index;

    while (start < end && text[start] == ' ') {
        ++start;
    }
    while (end > start && text[end - 1u] == ' ') {
        --end;
    }
    if (end - start >= 2u && text[start] == '"' && text[end - 1u] == '"') {
        ++start;
        --end;
    }
    if (end - start + 1u > capacity) {
        return V9X_FALSE;
    }
    for (index = 0u; index < end - start; ++index) {
        output[index] = text[start + index];
    }
    output[index] = '\0';
    return V9X_TRUE;
}

/* In one models section, the install section of the first model whose ID
 * list names device_id: lines are "description"=section,id[,id...]. */
static v9x_u16 v9x_inf_model_for(const char *text, v9x_u32 length,
                                 const char *models, const char *device_id,
                                 char *section, v9x_u32 capacity)
{
    struct v9x_inf_line line;
    v9x_u32 cursor = 0u;

    while (v9x_inf_next_line(text, length, models, &cursor, &line)) {
        v9x_u32 equals = 0u;
        v9x_u32 start;
        v9x_u32 position;
        v9x_u16 quoted = V9X_FALSE;
        v9x_u16 first = V9X_TRUE;
        char candidate[V9X_INF_SECTION_NAME_MAX];

        /* The '=' after the description, outside its quotes. */
        while (equals < line.length &&
               (quoted || line.start[equals] != '=')) {
            if (line.start[equals] == '"') {
                quoted = quoted ? V9X_FALSE : V9X_TRUE;
            }
            ++equals;
        }
        if (equals == line.length) {
            continue;
        }
        candidate[0] = '\0';
        start = equals + 1u;
        for (position = start; position <= line.length; ++position) {
            char id[V9X_INF_NAME_MAX];

            if (position < line.length && line.start[position] != ',') {
                continue;
            }
            if (first) {
                if (!v9x_inf_copy_span(line.start, start, position,
                                       candidate, sizeof(candidate))) {
                    break;
                }
                first = V9X_FALSE;
            } else if (v9x_inf_copy_span(line.start, start, position, id,
                                         sizeof(id)) &&
                       id[0] != '\0' && candidate[0] != '\0') {
                v9x_u32 id_length = 0u;

                while (id[id_length] != '\0') {
                    ++id_length;
                }
                if (v9x_inf_span_equals(id, id_length, device_id)) {
                    return v9x_inf_copy_text(section, capacity, candidate);
                }
            }
            start = position + 1u;
        }
    }
    return V9X_FALSE;
}

v9x_u16 v9x_inf_find_section(const char *text, v9x_u32 length,
                             const char *device_id,
                             const char *old_section,
                             char *section, v9x_u32 capacity)
{
    struct v9x_inf_line line;
    v9x_u32 cursor = 0u;

    if (device_id[0] != '\0') {
        /* [Manufacturer]: Name=ModelsSection, one line per maker. */
        while (v9x_inf_next_line(text, length, "Manufacturer", &cursor,
                                 &line)) {
            char models[V9X_INF_SECTION_NAME_MAX];
            v9x_u32 equals = 0u;
            v9x_u32 end;

            while (equals < line.length && line.start[equals] != '=') {
                ++equals;
            }
            if (equals == line.length) {
                continue;
            }
            for (end = equals + 1u; end < line.length &&
                                    line.start[end] != ','; ++end) {
            }
            if (!v9x_inf_copy_span(line.start, equals + 1u, end, models,
                                   sizeof(models))) {
                continue;
            }
            if (v9x_inf_model_for(text, length, models, device_id, section,
                                  capacity)) {
                return V9X_TRUE;
            }
        }
    }

    /* No model names the device: the old section, if it is still here. */
    cursor = 0u;
    if (old_section[0] != '\0' &&
        v9x_inf_next_line(text, length, old_section, &cursor, &line)) {
        return v9x_inf_copy_text(section, capacity, old_section);
    }
    return V9X_FALSE;
}

v9x_u16 v9x_inf_plan(const char *text, v9x_u32 length,
                     const char *install_section,
                     struct v9x_inf_plan *plan)
{
    static struct v9x_inf_lists lists;
    struct v9x_inf_line line;
    v9x_u32 cursor = 0u;
    v9x_u32 index;
    v9x_u16 any = V9X_FALSE;

    plan->count = 0u;
    plan->copies = 0u;
    plan->error[0] = '\0';
    lists.copy_count = 0u;
    lists.del_count = 0u;
    lists.add_count = 0u;

    while (v9x_inf_next_line(text, length, install_section, &cursor,
                             &line)) {
        v9x_u32 equals = 0u;
        const char *value;
        v9x_u32 value_length;
        v9x_u16 ok = V9X_TRUE;

        any = V9X_TRUE;
        while (equals < line.length && line.start[equals] != '=') {
            ++equals;
        }
        if (equals == line.length) {
            v9x_inf_fail(plan, "unsupported install line", line.start,
                         line.length);
            return V9X_FALSE;
        }
        value = line.start + equals + 1u;
        value_length = line.length - equals - 1u;
        while (equals != 0u && line.start[equals - 1u] == ' ') {
            --equals;
        }
        if (v9x_inf_span_equals(line.start, equals, "CopyFiles")) {
            /* "@file" copies one named file with no section; the
             * generator never writes it. */
            ok = value_length != 0u && value[0] != '@' &&
                 v9x_inf_section_list(value, value_length, lists.copy,
                                      &lists.copy_count);
        } else if (v9x_inf_span_equals(line.start, equals, "DelReg")) {
            ok = v9x_inf_section_list(value, value_length, lists.del,
                                      &lists.del_count);
        } else if (v9x_inf_span_equals(line.start, equals, "AddReg")) {
            ok = v9x_inf_section_list(value, value_length, lists.add,
                                      &lists.add_count);
        } else if (!v9x_inf_span_equals(line.start, equals, "LogConfig")) {
            ok = V9X_FALSE;
        }
        if (!ok) {
            v9x_inf_fail(plan, "unsupported install line", line.start,
                         line.length);
            return V9X_FALSE;
        }
    }
    if (!any) {
        v9x_inf_fail(plan, "no install section", install_section,
                     (v9x_u32)sizeof(plan->error));
        return V9X_FALSE;
    }

    for (index = 0u; index < lists.copy_count; ++index) {
        if (!v9x_inf_plan_copies(text, length, lists.copy[index], plan)) {
            return V9X_FALSE;
        }
    }
    for (index = 0u; index < lists.del_count; ++index) {
        if (!v9x_inf_plan_registry(text, length, lists.del[index],
                                   V9X_FALSE, plan)) {
            return V9X_FALSE;
        }
    }
    for (index = 0u; index < lists.add_count; ++index) {
        if (!v9x_inf_plan_registry(text, length, lists.add[index],
                                   V9X_TRUE, plan)) {
            return V9X_FALSE;
        }
    }
    if (plan->copies == 0u) {
        v9x_inf_fail(plan, "the install section copies no files", 0, 0u);
        return V9X_FALSE;
    }
    return V9X_TRUE;
}
