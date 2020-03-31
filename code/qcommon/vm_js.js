var LibraryVM = {
	$VM__deps: ['$SYSC', 'Cvar_VariableString'],
	$VM: {
		vmHeader_t: {
			__size__: 36,
			vmMagic: 0,
			instructionCount: 4,
			codeOffset: 8,
			codeLength: 12,
			dataOffset: 16,
			dataLength: 20,
			litLength: 24,
			bssLength: 28,
			jtrgLength: 32
		},
		vm_t: {
			__size__: 156,
			programStack: 0,
			systemCall: 4,
			name: 8,
			searchPath: 72,
			dllHandle: 76,
			entryPoint: 80,
			destroy: 84,
			currentlyInterpreting: 88,
			compiled: 92,
			codeBase: 96,
			entryOfs: 100,
			codeLength: 104,
			instructionPointers: 108,
			instructionCount: 112,
			dataBase: 116,
			dataMask: 120,
			stackBottom: 124,
			numSymbols: 128,
			symbols: 132,
			callLevel: 136,
			breakFunction: 140,
			breakCount: 144,
			jumpTableTargets: 148,
			numJumpTableTargets: 152
		},
		vms: [],
		SUSPENDED: 0xDEADBEEF,
		MAX_VMMAIN_ARGS: 13,
		ENTRY_FRAME_SIZE: 8 + 4 * 13,
		OPSTACK_SIZE: 1024,
		TYPE: {
			F4: 1,
			I4: 2,
			U4: 3
		},
		Constant4: function (state) {
			var v = ({{{ makeGetValue('state.codeBase', 'state.pc', 'i8') }}} & 0xff) |
				(({{{ makeGetValue('state.codeBase', 'state.pc+1', 'i8') }}} & 0xff) << 8) |
				(({{{ makeGetValue('state.codeBase', 'state.pc+2', 'i8') }}} & 0xff) << 16) |
				(({{{ makeGetValue('state.codeBase', 'state.pc+3', 'i8') }}} & 0xff) << 24 );
			state.pc += 4;
			return v;
		},
		Constant1: function (state) {
			var v = {{{ makeGetValue('state.codeBase', 'state.pc', 'i8') }}};
			state.pc += 1;
			return v;
		},
		FindLabels: function (state) {
			var labels = {};

			var op, lastop;
			for (state.instr = 0, state.pc = 0; state.instr < state.instructionCount; state.instr++) {
				op = {{{ makeGetValue('state.codeBase', 'state.pc', 'i8') }}};

				state.pc++;

				// create a label after each unconditional branching operator
				// FIXME this is a bit excessive
				if (lastop === 5 /* OP_CALL */ || lastop === 10 /* OP_JUMP */ || lastop === 7 /* OP_POP */ || lastop === 6 /* OP_PUSH */) {
					labels[state.instr] = true;
				}

				switch (op) {
					case 3 /* OP_ENTER */:
					case 4 /* OP_LEAVE */:
					case 9 /* OP_LOCAL */:
					case 34 /* OP_BLOCK_COPY */:
						VM.Constant4(state);
					break;

					case 8 /* OP_CONST */:
						var value = VM.Constant4(state);
						var nextop = {{{ makeGetValue('state.codeBase', 'state.pc', 'i8') }}};
						if (nextop === 10 /* OP_JUMP */) {
							labels[value] = true;
						}
						break;

					case 33 /* OP_ARG */:
						VM.Constant1(state);
					break;

					case 11 /* OP_EQ */:
					case 12 /* OP_NE */:
					case 13 /* OP_LTI */:
					case 14 /* OP_LEI */:
					case 15 /* OP_GTI */:
					case 16 /* OP_GEI */:
					case 17 /* OP_LTU */:
					case 18 /* OP_LEU */:
					case 19 /* OP_GTU */:
					case 20 /* OP_GEU */:
					case 21 /* OP_EQF */:
					case 22 /* OP_NEF */:
					case 23 /* OP_LTF */:
					case 24 /* OP_LEF */:
					case 25 /* OP_GTF */:
					case 26 /* OP_GEF */:
						// create labels for any explicit branch destination
						labels[VM.Constant4(state)] = true;
					break;

					default:
					break;
				}

				lastop = op;
			}

			return labels;
		},
		CompileModule: function (vmp, name, instructionCount, codeBase, dataBase) {
			var fs_game = UTF8ToString(_Cvar_VariableString(allocate(intArrayFromString('fs_game'), 'i8', ALLOC_STACK)));

			var state = {
				name: name,
				instructionCount: instructionCount,
				codeBase: codeBase,
				dataBase: dataBase,
				pc: 0,
				instr: 0
			};

			var labels = VM.FindLabels(state);
			var fninstr = 0;
			var eof = false;
			var ab = new ArrayBuffer(4);
			var i32 = new Int32Array(ab);
			var u32 = new Uint32Array(ab);
			var f32 = new Float32Array(ab);
			var callargs = [];

			//
			// expressions
			//
			var exprStack = [];

			function PUSH_EXPR(expr) {
				exprStack.push(expr);
			}

			function POP_EXPR(type) {
				return exprStack.pop();
			}

			function CAST_STR(type, expr) {
				switch (type) {
					case VM.TYPE.F4:
						return '+(' + expr + ')';

					case VM.TYPE.I4:
						return '(' + expr + ')|0';

					case VM.TYPE.U4:
						return '(' + expr + ')>>>0';

					default:
						throw new Error('unexpected data type');
				}
			}

			function BITCAST_STR(type, expr) {
				if (type === expr.type) {
					return expr.toString();
				}

				if (expr.type === VM.TYPE.I4 && type === VM.TYPE.F4) {
					if (expr instanceof CNST) {
						i32[0] = expr.value;
						return CAST_STR(type, f32[0]);
					}

					if (expr instanceof LOAD4) {
						// by default, every pointer value is loaded from HEAP32
						// don't use the scratch array if we can load directly from HEAPF32
						return CAST_STR(type, '{{{ makeGetValue("' + OFFSET_STR(expr.addr) + '", 0, "float") }}}');
					}

					return CAST_STR(type, 'i32[0] = ' + expr + ', f32[0]');
				} else if (expr.type === VM.TYPE.U4 && type === VM.TYPE.F4) {
					return CAST_STR(type, 'u32[0] = ' + expr + ', f32[0]');
				} else if (expr.type === VM.TYPE.F4 && type === VM.TYPE.I4) {
					return CAST_STR(type, 'f32[0] = ' + expr + ', i32[0]');
				} else if (expr.type === VM.TYPE.U4 && type === VM.TYPE.I4) {
					return CAST_STR(type, expr.toString());
				} else if (expr.type === VM.TYPE.F4 && type === VM.TYPE.U4) {
					return CAST_STR(type, 'f32[0] = ' + expr + ', u32[0]');
				} else if (expr.type === VM.TYPE.I4 && type === VM.TYPE.U4) {
					return CAST_STR(type, expr.toString());
				} else {
					throw new Error('unsupported bitcast operands ' + expr.type + ' ' + type);
				}
			}

			function OFFSET_STR(expr) {
				if (expr instanceof CNST) {
					return state.dataBase + expr.value;
				} else if (expr instanceof LOCAL) {
					return state.dataBase + expr.offset + '+STACKTOP';
				}
				return state.dataBase + '+' + expr;
			}

			function CNST(value) {
				var ctor = CNST.ctor;
				if (!ctor) {
					ctor = CNST.ctor = function (value) {
						this.type = VM.TYPE.I4;
						this.value = value;
					};
					ctor.prototype = Object.create(CNST.prototype);
					ctor.prototype.toString = function () {
						return this.value.toString();
					};
				}
				return new ctor(value);
			}

			function LOCAL(offset) {
				var ctor = LOCAL.ctor;
				if (!ctor) {
					ctor = LOCAL.ctor = function (offset) {
						this.type = VM.TYPE.I4;
						this.offset = offset;
					};
					ctor.prototype = Object.create(LOCAL.prototype);
					ctor.prototype.toString = function () {
						return 'STACKTOP+' + this.offset.toString();
					};
				}
				return new ctor(offset);
			}

			function LOAD4(addr) {
				var ctor = LOAD4.ctor;
				if (!ctor) {
					ctor = LOAD4.ctor = function (addr) {
						this.type = VM.TYPE.I4;
						this.addr = addr;
					};
					ctor.prototype = Object.create(LOAD4.prototype);
					ctor.prototype.toString = function () {
						return '{{{ makeGetValue("' + OFFSET_STR(this.addr) + '", 0, "i32") }}}';
					};
				}
				return new ctor(addr);
			}

			function LOAD2(addr) {
				var ctor = LOAD2.ctor;
				if (!ctor) {
					ctor = LOAD2.ctor = function (addr) {
						this.type = VM.TYPE.I4;
						this.addr = addr;
					};
					ctor.prototype = Object.create(LOAD2.prototype);
					ctor.prototype.toString = function () {
						// TODO add makeGetValue u16
						return 'HEAPU16[' + OFFSET_STR(this.addr) + ' >> 1]';
					};
				}
				return new ctor(addr);
			}

			function LOAD1(addr) {
				var ctor = LOAD1.ctor;
				if (!ctor) {
					ctor = LOAD1.ctor = function (addr) {
						this.type = VM.TYPE.I4;
						this.addr = addr;
					};
					ctor.prototype = Object.create(LOAD1.prototype);
					ctor.prototype.toString = function () {
						// TODO add makeGetValue u8
						return 'HEAPU8[' + OFFSET_STR(this.addr) + ']';
					};
				}
				return new ctor(addr);
			}

			function UNARY(type, op, expr) {
				var ctor = UNARY.ctor;
				if (!ctor) {
					ctor = UNARY.ctor = function (type, op, expr) {
						this.type = type;
						this.op = op;
						this.expr = expr;
					};
					ctor.prototype = Object.create(UNARY.prototype);
					ctor.prototype.toString = function () {
						var expr = BITCAST_STR(this.type, this.expr);

						switch (this.op) {
							case 35 /* OP_SEX8 */:
								return '((' + expr + ')<<24)>>24';

							case 36 /* OP_SEX16 */:
								return '((' + expr + ')<<16)>>16';

							case 37 /* OP_NEGI */:
								return '-(' + expr + ')';

							case 49 /* OP_BCOM */:
								return '(' + expr + ')^-1';

							case 53 /* OP_NEGF */:
								return '(-.0)-(' + expr + ')';

							default:
								throw new Error('unknown op type for unary expression');
						}
					};
				}
				return new ctor(type, op, expr);
			}

			function BINARY(type, op, lhs, rhs) {
				var ctor = BINARY.ctor;
				if (!ctor) {
					ctor = BINARY.ctor = function (type, op, lhs, rhs) {
						this.type = type;
						this.op = op;
						this.lhs = lhs;
						this.rhs = rhs;
					};
					ctor.prototype = Object.create(BINARY.prototype);
					ctor.prototype.toString = function () {
						var lhs = '(' + BITCAST_STR(this.type, this.lhs) + ')';
						var rhs = '(' + BITCAST_STR(this.type, this.rhs) + ')';

						switch (this.op) {
							case 38 /* OP_ADD */:
							case 54 /* OP_ADDF */:
								return lhs + '+' + rhs;

							case 39 /* OP_SUB */:
							case 55 /* OP_SUBF */:
								return lhs + '-' + rhs;

							case 40 /* OP_DIVI */:
							case 41 /* OP_DIVU */:
							case 56 /* OP_DIVF */:
								return lhs + '/' + rhs;

							case 42 /* OP_MODI */:
							case 43 /* OP_MODU */:
								return lhs + '%' + rhs;

							case 44 /* OP_MULI */:
							case 45 /* OP_MULU */:
								return 'Math.imul(' + lhs + ', ' + rhs +')';

							case 57 /* OP_MULF */:
								return lhs + '*' + rhs;

							case 46 /* OP_BAND */:
								return lhs + '&' + rhs;

							case 47 /* OP_BOR */:
								return lhs + '|' + rhs;

							case 48 /* OP_BXOR */:
								return lhs + '^' + rhs;

							case 50 /* OP_LSH */:
								return lhs + '<<' + rhs;

							case 51 /* OP_RSHI */:
								return lhs + '>>' + rhs;

							case 52 /* OP_RSHU */:
								return lhs + '>>>' + rhs;

							default:
								throw new Error('unknown op type for binary expression');
						}
					};
				}
				return new ctor(type, op, lhs, rhs);
			}

			function CONVERT(type, from_type, expr) {
				var ctor = CONVERT.ctor;
				if (!ctor) {
					ctor = CONVERT.ctor = function (type, from_type, expr) {
						this.type = type;
						this.from_type = from_type;
						this.expr = expr;
					};
					ctor.prototype = Object.create(CONVERT.prototype);
					ctor.prototype.toString = function () {
						return CAST_STR(this.type, BITCAST_STR(this.from_type, this.expr));
					};
				}
				return new ctor(type, from_type, expr);
			}

			//
			// statements
			//
			var moduleStr = '';
			var indent = 0;

			function EmitStatement(str) {
				var prefix = '';
				for (var i = 0; i < indent; i++) {
					prefix += '\t';
				}
				moduleStr += prefix + str + '\n';
			}

			function EmitEnter(frameSize) {
				EmitStatement('var fn' + fninstr + ' = FUNCTIONS[' + fninstr + '] = function fn' + fninstr + '(override) {');
				indent++;
				EmitStatement('var label = override || ' + fninstr + ';');
				EmitStatement('while (1) switch (label) {');
				indent++;
				EmitStatement('case ' + fninstr + ':');
				indent++;
				EmitStatement('STACKTOP -= ' + frameSize + ';');
			}

			function EmitLeave(frameSize, ret) {
				// leave the return value on the stack
				EmitStatement('{{{ makeSetValue("' + OFFSET_STR(LOCAL(frameSize - 4)) + '", 0, "' + ret + '", "i32") }}};');
				EmitStatement('STACKTOP += ' + frameSize + ';');
				EmitStatement('return;');

				if (eof) {
					indent--;
					indent--;
					EmitStatement('}');
					indent--;
					EmitStatement('};');
				}
			}

			function EmitCall(addr) {
				var translate = {
					'cgame': {
						'-101': 'memset',
						'-102': 'memcpy',
						'-103': 'strncpy',
						'-104': 'sin',
						'-105': 'cos',
						'-106': 'atan2',
						'-107': 'sqrt',
						'-108': 'floor',
						'-109': 'ceil',
						'-112': 'acos'
					},
					'qagame': {
						'-101': 'memset',
						'-102': 'memcpy',
						'-103': 'strncpy',
						'-104': 'sin',
						'-105': 'cos',
						'-106': 'atan2',
						'-107': 'sqrt',
						'-111': 'floor',
						'-112': 'ceil'
					},
					'ui': {
						'-101': 'memset',
						'-102': 'memcpy',
						'-103': 'strncpy',
						'-104': 'sin',
						'-105': 'cos',
						'-106': 'atan2',
						'-107': 'sqrt',
						'-108': 'floor',
						'-109': 'ceil'
					},
				};

				// emit return address info
				EmitStore4(LOCAL(0), fninstr);
				EmitStore4(LOCAL(4), state.instr + 1);

				// emit args
				while (callargs.length) {
					var arg = callargs.shift();
					EmitStore4(arg.addr, arg.value);
				}

				// go ahead and directly translate a few syscalls to speed things up
				var table = translate[state.name];
				var translation = table && table[addr];

				if (translation) {
					switch (translation) {
						case 'memset':
							EmitStatement('{{{ makeSetValue("' + OFFSET_STR(LOCAL(-4)) + '", 0, "_memset(' + state.dataBase + '+' + LOAD4(LOCAL(8)) + ', ' + LOAD4(LOCAL(12)) + ', ' + LOAD4(LOCAL(16)) + ')", "i32") }}};');
						break;

						case 'memcpy':
							EmitStatement('{{{ makeSetValue("' + OFFSET_STR(LOCAL(-4)) + '", 0, "_memcpy(' + state.dataBase + '+' + LOAD4(LOCAL(8)) + ', ' + state.dataBase + '+' + LOAD4(LOCAL(12)) + ', ' + LOAD4(LOCAL(16)) + ')", "i32") }}};');
						break;

						case 'strncpy':
							EmitStatement('{{{ makeSetValue("' + OFFSET_STR(LOCAL(-4)) + '", 0, "_strncpy(' + state.dataBase + '+' + LOAD4(LOCAL(8)) + ', ' + state.dataBase + '+' + LOAD4(LOCAL(12)) + ', ' + LOAD4(LOCAL(16)) + ')", "i32") }}};');
						break;

						case 'sin':
							EmitStatement('{{{ makeSetValue("' + OFFSET_STR(LOCAL(-4)) + '", 0, "Math.sin(' + (BITCAST_STR(VM.TYPE.F4, LOAD4(LOCAL(8)))) + ')", "float") }}};');
						break;

						case 'cos':
							EmitStatement('{{{ makeSetValue("' + OFFSET_STR(LOCAL(-4)) + '", 0, "Math.cos(' + (BITCAST_STR(VM.TYPE.F4, LOAD4(LOCAL(8)))) + ')", "float") }}};');
						break;

						case 'atan2':
							EmitStatement('{{{ makeSetValue("' + OFFSET_STR(LOCAL(-4)) + '", 0, "Math.atan2(' + (BITCAST_STR(VM.TYPE.F4, LOAD4(LOCAL(8)))) + ', ' + (BITCAST_STR(VM.TYPE.F4, LOAD4(LOCAL(12)))) + ')", "float") }}};');
						break;

						case 'sqrt':
							EmitStatement('{{{ makeSetValue("' + OFFSET_STR(LOCAL(-4)) + '", 0, "Math.sqrt(' + (BITCAST_STR(VM.TYPE.F4, LOAD4(LOCAL(8)))) + ')", "float") }}};');
						break;

						case 'floor':
							EmitStatement('{{{ makeSetValue("' + OFFSET_STR(LOCAL(-4)) + '", 0, "Math.floor(' + (BITCAST_STR(VM.TYPE.F4, LOAD4(LOCAL(8)))) + ')", "float") }}};');
						break;

						case 'ceil':
							EmitStatement('{{{ makeSetValue("' + OFFSET_STR(LOCAL(-4)) + '", 0, "Math.ceil(' + (BITCAST_STR(VM.TYPE.F4, LOAD4(LOCAL(8)))) + ')", "float") }}};');
						break;

						case 'acos':
							EmitStatement('{{{ makeSetValue("' + OFFSET_STR(LOCAL(-4)) + '", 0, "Math.acos(' + (BITCAST_STR(VM.TYPE.F4, LOAD4(LOCAL(8)))) + ')", "float") }}};');
						break;
					}
				} else {
					var expr = 'call(' + addr + ')';

					// remove the indirection if we can
					if (addr instanceof CNST) {
						if (addr.value >= 0) {
							expr = 'fn' + addr.value + '()';
						} else {
							expr = 'syscall(' + addr.value + ')';
						}
					}

					EmitStatement(expr + ';');
				}

				// push return value to stack
				PUSH_EXPR(LOAD4(LOCAL(-4)));
			}

			function EmitJump(label) {
				EmitStatement('label = ' + label + ';');
				EmitStatement('break;');
			}

			function EmitConditionalJump(lhs, rhs, cond, label) {
				var expr = '(' + lhs + ') ' + cond + ' (' + rhs + ')';

				// MEGA HACK FOR CPMA 1.47
				// ignore its built in pak-file checking since we repackage our own paks
				if (fs_game === 'cpma' && name === 'qagame' && (state.instr === 1382 || state.instr === 1392)) {
					// 1382 is checking if trap_FS_FOpenFile returned 0 for the pak, and if so, jumps to an error block
					// 1392 is checking if trap_FS_FOpenFile's returned length matches the expected length and if so, jumps to a success block
					expr = state.instr === 1382 ? '0' : '1';
				}

				EmitStatement('if (' + expr + ') {');
				indent++;
				EmitJump(label);
				indent--;
				EmitStatement('}');
			}

			function EmitStore4(addr, value) {
				if (value.type === VM.TYPE.F4) {
					EmitStatement('{{{ makeSetValue("' + OFFSET_STR(addr) + '", 0, "' + value + '", "float") }}};');
				} else {
					EmitStatement('{{{ makeSetValue("' + OFFSET_STR(addr) + '", 0, "' + value + '", "i32") }}};');
				}
			}

			function EmitStore2(addr, value) {
				EmitStatement('{{{ makeSetValue("' + OFFSET_STR(addr) + '", 0, "' + value + '", "i16") }}};');
			}

			function EmitStore1(addr, value) {
				EmitStatement('{{{ makeSetValue("' + OFFSET_STR(addr) + '", 0, "' + value + '", "i8") }}};');
			}

			function EmitBlockCopy(dest, src, bytes) {
				EmitStatement('{{{ makeCopyValues("' + OFFSET_STR(dest) + '", "' + OFFSET_STR(src) + '", "' + bytes + '", "i8") }}};');
			}

			EmitStatement('(function () {');
			indent++;

			EmitStatement('var FUNCTIONS = {};');
			EmitStatement('var STACKTOP;');

			EmitStatement('function syscall(callnum) {');
			EmitStatement('\tcallnum = ~callnum;');
			EmitStatement('\t// save the current vm');
			//EmitStatement('\tvar savedVM = _VM_GetCurrent();');
			EmitStatement('\tvar stackOnEntry = STACKTOP;');
			EmitStatement('\tvar image = {{{ makeGetValue("vmp", "VM.vm_t.dataBase", "i32") }}};');
			EmitStatement('\t// store the callnum in the return address space');
			EmitStatement('\tvar returnAddr = {{{ makeGetValue("image", "stackOnEntry + 4", "i32") }}};');
			EmitStatement('\t{{{ makeSetValue("image", "stackOnEntry + 4", "callnum", "i32") }}};');
			// MEGA HACK FOR CPMA 1.47
			// it uses the default model mynx which we don't have. if
			// it fails to load the default model, the game will exit
			if (fs_game === 'cpma' && name === 'cgame') {
				EmitStatement('\tif (callnum === 10 /* trap_FS_FOpenFile */ || callnum === 34 /* trap_S_RegisterSound */ || callnum === 37 /* trap_R_RegisterModel */ || callnum === 38 /* trap_R_RegisterSkin */) {');
				EmitStatement('\t\tvar modelName = UTF8ToString(' + state.dataBase + ' + {{{ makeGetValue("image", "stackOnEntry + 8", "i32") }}});');
				EmitStatement('\t\tif (modelName.indexOf("/mynx") !== -1) {');
				EmitStatement('\t\t\tmodelName = modelName.replace("/mynx", "/sarge");');
				EmitStatement('\t\t\tSTACKTOP -= modelName.length+1;');
				EmitStatement('\t\t\stringToUTF8(modelName, ' + state.dataBase + ' + STACKTOP, modelName.length+1);');
				EmitStatement('\t\t\t{{{ makeSetValue("image", "stackOnEntry + 8", "STACKTOP", "i32") }}};');
				EmitStatement('\t\t}');
				EmitStatement('\t}');
			}
			EmitStatement('\t// modify VM stack pointer for recursive VM entry');
			EmitStatement('\tSTACKTOP -= 4;')
			EmitStatement('\t{{{ makeSetValue("vmp", "VM.vm_t.programStack", "STACKTOP", "i32") }}};');
			EmitStatement('\t// call into the client');
			EmitStatement('\tvar systemCall = {{{ makeGetValue("vmp", "VM.vm_t.systemCall", "i32*") }}};');
			EmitStatement('\tvar ret = dynCall("ii", systemCall, [image + stackOnEntry + 4]);');
			EmitStatement('\t{{{ makeSetValue("image", "stackOnEntry + 4", "returnAddr", "i32") }}};');
			EmitStatement('\t// leave the return value on the stack');
			EmitStatement('\t{{{ makeSetValue("image", "stackOnEntry - 4", "ret", "i32") }}};');
			EmitStatement('\tSTACKTOP = stackOnEntry;');
			EmitStatement('\t{{{ makeSetValue("vmp", "VM.vm_t.programStack", "STACKTOP", "i32") }}};');
			//EmitStatement('\t_VM_SetCurrent(savedVM);');
			// intercept trap_UpdateScreen calls coming from cgame and suspend the VM
			if (name === 'cgame') {
				EmitStatement('\tif (callnum === 17 /* trap_UpdateScreen */) {');
				EmitStatement('\t\tthrow { suspend: true };');
				EmitStatement('\t}');
			}
			EmitStatement('\treturn;');
			EmitStatement('}');

			EmitStatement('function call(addr) {');
			EmitStatement('\tif (addr >= 0) {');
			EmitStatement('\t\tvar fn = FUNCTIONS[addr];');
			EmitStatement('\t\tfn();');
			EmitStatement('\t\treturn;');
			EmitStatement('\t}');
			EmitStatement('\tsyscall(addr);');
			EmitStatement('}');

			EmitStatement('var ab = new ArrayBuffer(4);');
			EmitStatement('var i32 = new Int32Array(ab);');
			EmitStatement('var u32 = new Uint32Array(ab);');
			EmitStatement('var f32 = new Float32Array(ab);');

			var lastop1, lastop2;
			for (state.instr = 0, state.pc = 0; state.instr < state.instructionCount; state.instr++) {
				var op = {{{ makeGetValue('state.codeBase', 'state.pc', 'i8') }}};

				state.pc++;

				if (labels[state.instr]) {
					indent--;
					EmitStatement('case ' + state.instr + ':');
					indent++;
				}

				switch (op) {
					//
					// expressions
					//
					case 6 /* OP_PUSH */:
						PUSH_EXPR(CNST(0));
						eof = true;
					break;

					case 7 /* OP_POP */:
						POP_EXPR();
					break;

					case 8 /* OP_CONST */:
						PUSH_EXPR(CNST(VM.Constant4(state)));
					break;

					case 9 /* OP_LOCAL */:
						PUSH_EXPR(LOCAL(VM.Constant4(state)));
					break;

					case 27 /* OP_LOAD1 */:
						PUSH_EXPR(LOAD1(POP_EXPR()));
					break;

					case 28 /* OP_LOAD2 */:
						PUSH_EXPR(LOAD2(POP_EXPR()));
					break;

					case 29 /* OP_LOAD4 */:
						PUSH_EXPR(LOAD4(POP_EXPR()));
					break;

					case 35 /* OP_SEX8 */:
					case 36 /* OP_SEX16 */:
					case 37 /* OP_NEGI */:
					case 49 /* OP_BCOM */:
						PUSH_EXPR(UNARY(VM.TYPE.I4, op, POP_EXPR()));
					break;

					case 53 /* OP_NEGF */:
						PUSH_EXPR(UNARY(VM.TYPE.F4, op, POP_EXPR()));
					break;

					case 38 /* OP_ADD */:
					case 39 /* OP_SUB */:
					case 40 /* OP_DIVI */:
					case 42 /* OP_MODI */:
					case 44 /* OP_MULI */:
					case 46 /* OP_BAND */:
					case 47 /* OP_BOR */:
					case 48 /* OP_BXOR */:
					case 50 /* OP_LSH */:
					case 51 /* OP_RSHI */:
						var rhs = POP_EXPR();
						var lhs = POP_EXPR();
						PUSH_EXPR(BINARY(VM.TYPE.I4, op, lhs, rhs));
					break;

					case 41 /* OP_DIVU */:
					case 43 /* OP_MODU */:
					case 45 /* OP_MULU */:
					case 52 /* OP_RSHU */:
						var rhs = POP_EXPR();
						var lhs = POP_EXPR();
						PUSH_EXPR(BINARY(VM.TYPE.U4, op, lhs, rhs));
					break;

					case 54 /* OP_ADDF */:
					case 55 /* OP_SUBF */:
					case 56 /* OP_DIVF */:
					case 57 /* OP_MULF */:
						var rhs = POP_EXPR();
						var lhs = POP_EXPR();
						PUSH_EXPR(BINARY(VM.TYPE.F4, op, lhs, rhs));
					break;

					case 58 /* OP_CVIF */:
						PUSH_EXPR(CONVERT(VM.TYPE.F4, VM.TYPE.I4, POP_EXPR()));
					break;

					case 59 /* OP_CVFI */:
						PUSH_EXPR(CONVERT(VM.TYPE.I4, VM.TYPE.F4, POP_EXPR()));
					break;

					//
					// statements
					//
					case 0 /* OP_UNDEF */:
					case 1 /* OP_IGNORE */:
					break;

					case 2 /* OP_BREAK */:
						EmitStatement('debugger;');
					break;

					case 3 /* OP_ENTER */:
						fninstr = state.instr;
						eof = false;
						EmitEnter(VM.Constant4(state));
					break;

					case 4 /* OP_LEAVE */:
						EmitLeave(VM.Constant4(state), BITCAST_STR(VM.TYPE.I4, POP_EXPR()));
					break;

					case 5 /* OP_CALL */:
						EmitCall(POP_EXPR());
					break;

					case 10 /* OP_JUMP */:
						// OP_LEAVE ops have explicit jumps written out afterwards that we can ignore
						// RETI4
						// ADDRGP4 $1
						// JUMPV
						var expr = POP_EXPR();
						if (!(lastop1 === 4 /* OP_LEAVE */ && lastop2 === 8 /* OP_CONST */)) {
							var instr = BITCAST_STR(VM.TYPE.I4, expr);
							EmitJump(instr);
						}
					break;

					case 11 /* OP_EQ */:
						var rhs = BITCAST_STR(VM.TYPE.I4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.I4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '===', VM.Constant4(state));
					break;

					case 12 /* OP_NE */:
						var rhs = BITCAST_STR(VM.TYPE.I4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.I4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '!==', VM.Constant4(state));
					break;

					case 13 /* OP_LTI */:
						var rhs = BITCAST_STR(VM.TYPE.I4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.I4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '<', VM.Constant4(state));
					break;

					case 14 /* OP_LEI */:
						var rhs = BITCAST_STR(VM.TYPE.I4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.I4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '<=', VM.Constant4(state));
					break;

					case 15 /* OP_GTI */:
						var rhs = BITCAST_STR(VM.TYPE.I4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.I4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '>', VM.Constant4(state));
					break;

					case 16 /* OP_GEI */:
						var rhs = BITCAST_STR(VM.TYPE.I4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.I4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '>=', VM.Constant4(state));
					break;

					case 17 /* OP_LTU */:
						var rhs = BITCAST_STR(VM.TYPE.U4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.U4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '<', VM.Constant4(state));
					break;

					case 18 /* OP_LEU */:
						var rhs = BITCAST_STR(VM.TYPE.U4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.U4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '<=', VM.Constant4(state));
					break;

					case 19 /* OP_GTU */:
						var rhs = BITCAST_STR(VM.TYPE.U4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.U4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '>', VM.Constant4(state));
					break;

					case 20 /* OP_GEU */:
						var rhs = BITCAST_STR(VM.TYPE.U4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.U4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '>=', VM.Constant4(state));
					break;

					case 21 /* OP_EQF */:
						var rhs = BITCAST_STR(VM.TYPE.F4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.F4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '===', VM.Constant4(state));
					break;

					case 22 /* OP_NEF */:
						var rhs = BITCAST_STR(VM.TYPE.F4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.F4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '!==', VM.Constant4(state));
					break;

					case 23 /* OP_LTF */:
						var rhs = BITCAST_STR(VM.TYPE.F4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.F4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '<', VM.Constant4(state));
					break;

					case 24 /* OP_LEF */:
						var rhs = BITCAST_STR(VM.TYPE.F4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.F4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '<=', VM.Constant4(state));
					break;

					case 25 /* OP_GTF */:
						var rhs = BITCAST_STR(VM.TYPE.F4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.F4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '>', VM.Constant4(state));
					break;

					case 26 /* OP_GEF */:
						var rhs = BITCAST_STR(VM.TYPE.F4, POP_EXPR());
						var lhs = BITCAST_STR(VM.TYPE.F4, POP_EXPR());
						EmitConditionalJump(lhs, rhs, '>=', VM.Constant4(state));
					break;

					case 30 /* OP_STORE1 */:
						var value = BITCAST_STR(VM.TYPE.I4, POP_EXPR());
						var addr = POP_EXPR();
						EmitStore1(addr, value);
					break;

					case 31 /* OP_STORE2 */:
						var value = BITCAST_STR(VM.TYPE.I4, POP_EXPR());
						var addr = POP_EXPR();
						EmitStore2(addr, value);
					break;

					case 32 /* OP_STORE4 */:
						var value = POP_EXPR();
						var addr = POP_EXPR();
						EmitStore4(addr, value);
					break;

					case 33 /* OP_ARG */:
						var value = POP_EXPR();
						var addr = LOCAL(VM.Constant1(state));
						callargs.push({ addr: addr, value: value });
					break;

					case 34 /* OP_BLOCK_COPY */:
						var src = POP_EXPR();
						var dest = POP_EXPR();
						var bytes = VM.Constant4(state);
						EmitBlockCopy(dest, src, bytes);
					break;
				}

				lastop1 = lastop2;
				lastop2 = op;
			}

			EmitStatement('return Object.create(Object.prototype, {');
			EmitStatement('\tFUNCTIONS: { value: FUNCTIONS },');
			EmitStatement('\tSTACKTOP: { get: function () { return STACKTOP; }, set: function (val) { STACKTOP = val; } },');
			EmitStatement('});');
			indent--;
			EmitStatement('})');

			return moduleStr;
		},
	},
	VM_Compile__sig: 'vii',
	VM_Compile__deps: ['$SYSC', '$VM', 'VM_Destroy'],
	VM_Compile: function (vmp, headerp) {
		//var current = _VM_GetCurrent();
		var name = UTF8ToString(vmp + VM.vm_t.name);
		var dataBase = {{{ makeGetValue('vmp', 'VM.vm_t.dataBase', 'i8*') }}};
		var codeOffset = {{{ makeGetValue('headerp', 'VM.vmHeader_t.codeOffset', 'i32') }}};
		var instructionCount = {{{ makeGetValue('headerp', 'VM.vmHeader_t.instructionCount', 'i32') }}};

		var vm;
		try {
			var start = Date.now();

			var module = VM.CompileModule(vmp, name, instructionCount, headerp + codeOffset, dataBase);
			vm = eval(module)();

			SYSC.Print('VM file ' + name + ' compiled in ' + (Date.now() - start) + ' milliseconds');
		} catch (e) {
			if (e.longjmp || e === 'longjmp') {
				throw e;
			}
			SYSC.Error('fatal', e);
		}

		var handle = VM.vms.length+1;
		VM.vms[handle] = vm;

		if (!VM.DestroyPtr) {
			VM.DestroyPtr = addFunction(_VM_Destroy ,'vi');
		}

		{{{ makeSetValue('vmp', 'VM.vm_t.entryOfs', 'handle', 'i32') }}};
		{{{ makeSetValue('vmp', 'VM.vm_t.destroy', 'VM.DestroyPtr', 'void*') }}};
	},
	VM_Destroy: function (vmp) {
		var handle = {{{ makeGetValue('vmp', 'VM.vm_t.entryOfs', 'i32') }}};

		delete VM.vms[handle];
	},
	VM_CallCompiled__sig: 'iii',
	VM_CallCompiled__deps: ['$SYSC', '$VM', 'VM_SuspendCompiled'],
	VM_CallCompiled: function (vmp, args) {
		var handle = {{{ makeGetValue('vmp', 'VM.vm_t.entryOfs', 'i32') }}};
		var vm = VM.vms[handle];

		// we can't re-enter the vm until it's been resumed
		if (vm.suspended) {
			SYSC.Error('drop', 'attempted to re-enter suspended vm');
		}

		// set the current vm
		//var savedVM = _VM_GetCurrent();
		//_VM_SetCurrent(vmp);

		// save off the stack pointer
		var image = {{{ makeGetValue('vmp', 'VM.vm_t.dataBase', 'i32') }}};

		// set up the stack frame
		var stackOnEntry = {{{ makeGetValue('vmp', 'VM.vm_t.programStack', 'i32') }}};
		var stackTop = stackOnEntry - VM.ENTRY_FRAME_SIZE;

		{{{ makeSetValue('image', 'stackTop', '-1', 'i32') }}};
		{{{ makeSetValue('image', 'stackTop + 4', '0', 'i32') }}};

		for (var i = 0; i < VM.MAX_VMMAIN_ARGS; i++) {
			var arg = {{{ makeGetValue('args', 'i * 4', 'i32' )}}};
			{{{ makeSetValue('image', 'stackTop + 8 + i * 4', 'arg', 'i32') }}};
		}

		// call into the entry point
		var result;

		try {
			var entryPoint = vm.FUNCTIONS[0];

			vm.STACKTOP = stackTop;

			entryPoint();

			if (vm.STACKTOP !== (stackOnEntry - VM.ENTRY_FRAME_SIZE)) {
				SYSC.Error('fatal', 'program stack corrupted, is ' + vm.STACKTOP + ', expected ' + (stackOnEntry - VM.ENTRY_FRAME_SIZE));
			}

			result = {{{ makeGetValue('image', 'vm.STACKTOP - 4', 'i32') }}};

			{{{ makeSetValue('vmp', 'VM.vm_t.programStack', 'stackOnEntry', 'i32') }}};
		} catch (e) {
			if (e.longjmp || e === 'longjmp') {
				throw e;
			}

			if (!e.suspend) {
				SYSC.Error('fatal', e);
				return;
			}

			_VM_SuspendCompiled(vmp, stackOnEntry);

			result = VM.SUSPENDED;
		}

		// restore the current vm
		//_VM_SetCurrent(savedVM);

		// return value is at the top of the stack still
		return result;
	},
	VM_IsSuspendedCompiled__deps: ['$SYSC'],
	VM_IsSuspendedCompiled: function (vmp) {
		var handle = {{{ makeGetValue('vmp', 'VM.vm_t.entryOfs', 'i32') }}};
		var vm = VM.vms[handle];

		if (!vm) {
			SYSC.Error('drop', 'invalid vm handle');
			return;
		}

		return vm.suspended;
	},
	VM_SuspendCompiled__deps: ['$SYSC'],
	VM_SuspendCompiled: function (vmp, stackOnEntry) {
		var handle = {{{ makeGetValue('vmp', 'VM.vm_t.entryOfs', 'i32') }}};
		var vm = VM.vms[handle];

		if (!vm) {
			SYSC.Error('drop', 'invalid vm handle');
			return;
		}

		vm.suspended = true;
		vm.stackOnEntry = stackOnEntry;
	},
	VM_ResumeCompiled__deps: ['$SYSC', 'VM_SuspendCompiled'],
	VM_ResumeCompiled: function (vmp) {
		var handle = {{{ makeGetValue('vmp', 'VM.vm_t.entryOfs', 'i32') }}};
		var vm = VM.vms[handle];

		if (!vm) {
			SYSC.Error('drop', 'invalid vm handle');
			return;
		}

		//var savedVM = _VM_GetCurrent();
		//_VM_SetCurrent(vmp);

		var image = {{{ makeGetValue('vmp', 'VM.vm_t.dataBase', 'i32') }}};
		var stackOnEntry = vm.stackOnEntry;
		var result;

		vm.suspended = false;

		try {
			while (true) {
				// grab the last return address off the stack top and resume execution
				var fninstr = {{{ makeGetValue('image', 'vm.STACKTOP', 'i32') }}};
				var opinstr = {{{ makeGetValue('image', 'vm.STACKTOP + 4', 'i32') }}};

				if (fninstr === -1) {
					// we're done unwinding
					break;
				}

				var fn = vm.FUNCTIONS[fninstr];

				fn(opinstr);
			}

			if (vm.STACKTOP !== (stackOnEntry - VM.ENTRY_FRAME_SIZE)) {
				SYSC.Error('drop', 'program stack corrupted, is ' + vm.STACKTOP + ', expected ' + (stackOnEntry - VM.ENTRY_FRAME_SIZE));
				return;
			}

			result = {{{ makeGetValue('image', 'vm.STACKTOP - 4', 'i32') }}};

			{{{ makeSetValue('vmp', 'VM.vm_t.programStack', 'stackOnEntry', 'i32') }}};
		} catch (e) {
			if (e.longjmp || e === 'longjmp') {
				throw e;
			}

			if (!e.suspend) {
				SYSC.Error('drop', e);
				return;
			}

			_VM_SuspendCompiled(vmp, stackOnEntry);

			result = VM.SUSPENDED;
		}

		// restore the current vm
		//_VM_SetCurrent(savedVM);

		return result;
	}
};

mergeInto(LibraryManager.library, LibraryVM);
