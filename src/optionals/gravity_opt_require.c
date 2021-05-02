#include "gravity_vm.h"

#include "gravity_opt_require.h"
#include "gravity_macros.h"
#include "gravity_vmmacros.h"
#include "gravity_utils.h"
#include "gravity_compiler.h"

#include <errno.h>

#define READ_CHUNK_SIZE 1024

static gravity_closure_t        *gravity_closure_require = NULL;
static uint32_t                 refcount = 0;

////////////////////
// Implementation //
////////////////////

static bool require(gravity_vm *vm, gravity_value_t *args, uint16_t nargs, uint32_t rindex) {
    char *library_name;
    char file_name_gravity[512];
    char *source = malloc(1);
    char read_buffer[READ_CHUNK_SIZE];
    size_t size = 0;
    size_t chunk_size;
    gravity_compiler_t *compiler;
    gravity_closure_t *importedClosure;
    gravity_delegate_t *delegate;
    gravity_value_t export_fn;
    bool has_result;
    gravity_value_t export;
    
    if (nargs != 2 || !VALUE_ISA_STRING(args[1])) {
        RETURN_ERROR("require must be called with one string argument!");
    }
    
    library_name = VALUE_AS_CSTRING(args[1]);
    
    sprintf(file_name_gravity, "mymodule.gravity", library_name);
    
    if (!file_exists(file_name_gravity)) {
        RETURN_ERROR("File %s doesn't exist!", file_name_gravity);
    }
    
    int fd = open(file_name_gravity, O_RDONLY);
    if (fd < 0) {
        RETURN_ERROR("Couldn't open file %s: %s\n", file_name_gravity, strerror(errno));
    }
    
    do {
        chunk_size = read(fd, read_buffer, READ_CHUNK_SIZE);
        
        source = realloc(source, size + chunk_size);
        memcpy(source + size, read_buffer, chunk_size);
        
        size += chunk_size;
    } while (chunk_size == READ_CHUNK_SIZE);
    
    source = realloc(source, size + 1);
    source[size] = '\0';
    
    delegate = gravity_vm_delegate(vm);
    compiler = gravity_compiler_create(delegate);
    importedClosure = gravity_compiler_run(compiler, source, size, 0, false, true);
    
    gravity_compiler_transfer(compiler, vm);
    gravity_compiler_free(compiler);
    
    gravity_vm_runclosure(vm, importedClosure, VALUE_FROM_OBJECT(gravity_closure_require), NULL, 0); 
    
    // Get the export function from the library
    export_fn = gravity_vm_getvalue(vm, "export", 6);
    if (!VALUE_ISA_CLOSURE(export_fn)) {
        RETURN_ERROR("%s doesn't have an export function!", library_name);
    }
    
    has_result = gravity_vm_runclosure(vm, VALUE_AS_CLOSURE(export_fn), VALUE_FROM_OBJECT(gravity_closure_require), NULL, 0);
    if (!has_result) {
        RETURN_ERROR("%s's export function didn't return anything!", library_name);
    }
    
    export = gravity_vm_result(vm);
    
    RETURN_VALUE(export, rindex);
}

static void create_optional_closure(gravity_vm *vm) {
    gravity_function_t *requireFunc = gravity_function_new_internal(vm, "require", require, 1);
    gravity_closure_require = gravity_closure_new(vm, requireFunc);
}

void gravity_require_register (gravity_vm *vm) {
    if (!gravity_closure_require) create_optional_closure(vm);
    ++refcount;
    
    if (!vm || gravity_vm_ismini(vm)) return;
    gravity_vm_setvalue(vm, "require", VALUE_FROM_OBJECT(gravity_closure_require));
}

void gravity_require_free (void) {
    if (!gravity_closure_require) return;
    if (--refcount) return;
    
    gravity_closure_require = NULL;
}
