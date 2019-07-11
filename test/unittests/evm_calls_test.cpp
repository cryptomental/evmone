// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

#include "evm_fixture.hpp"
#include <intx/intx.hpp>
#include <test/utils/bytecode.hpp>

using namespace intx;

TEST_F(evm, delegatecall)
{
    auto code = std::string{};
    code += "6001600003600052";              // m[0] = 0xffffff...
    code += "600560046003600260016103e8f4";  // DELEGATECALL(1000, 0x01, ...)
    code += "60086000f3";

    auto call_output = bytes{0xa, 0xb, 0xc};
    call_result.output_data = call_output.data();
    call_result.output_size = call_output.size();
    call_result.gas_left = 1;

    execute(1700, code);

    EXPECT_EQ(gas_used, 1690);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);

    auto gas_left = 1700 - 736;
    ASSERT_EQ(recorded_calls.size(), 1);
    const auto& call_msg = recorded_calls.back();
    EXPECT_EQ(call_msg.gas, gas_left - gas_left / 64);
    EXPECT_EQ(call_msg.input_size, 3);

    ASSERT_EQ(result.output_size, 8);
    auto output = bytes_view{result.output_data, result.output_size};
    EXPECT_EQ(output, (bytes{0xff, 0xff, 0xff, 0xff, 0xa, 0xb, 0xc, 0xff}));
}

TEST_F(evm, delegatecall_static)
{
    // Checks if DELEGATECALL forwards the "static" flag.
    msg.flags = EVMC_STATIC;
    execute("60008080808080f4");
    ASSERT_EQ(recorded_calls.size(), 1);
    const auto& call_msg = recorded_calls.back();
    EXPECT_EQ(call_msg.gas, 0);
    EXPECT_EQ(call_msg.flags, EVMC_STATIC);
    EXPECT_EQ(gas_used, 718);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
}

TEST_F(evm, create)
{
    auto& account = accounts[{}];
    account.set_balance(1);

    auto call_output = bytes{0xa, 0xb, 0xc};
    call_result.output_data = call_output.data();
    call_result.output_size = call_output.size();
    call_result.create_address.bytes[10] = 0xcc;
    call_result.gas_left = 200000;
    execute(300000, "602060006001f0600155");

    EXPECT_EQ(gas_used, 115816);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);

    auto key = evmc_bytes32{};
    key.bytes[31] = 1;
    EXPECT_EQ(account.storage[key].value.bytes[22], 0xcc);

    ASSERT_EQ(recorded_calls.size(), 1);
    const auto& call_msg = recorded_calls.back();
    EXPECT_EQ(call_msg.input_size, 0x20);
}

TEST_F(evm, create_gas)
{
    auto c = size_t{0};
    for (auto r : {EVMC_HOMESTEAD, EVMC_TANGERINE_WHISTLE})
    {
        ++c;
        rev = r;
        execute(50000, "60008080f0");
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(gas_used, rev == EVMC_HOMESTEAD ? 50000 : 49719) << rev;
        ASSERT_EQ(recorded_calls.size(), c);
        EXPECT_EQ(recorded_calls.back().gas, rev == EVMC_HOMESTEAD ? 17991 : 17710) << rev;
    }
}

TEST_F(evm, create2)
{
    rev = EVMC_CONSTANTINOPLE;
    auto& account = accounts[{}];
    account.set_balance(1);

    auto call_output = bytes{0xa, 0xb, 0xc};
    call_result.output_data = call_output.data();
    call_result.output_size = call_output.size();
    call_result.create_address.bytes[10] = 0xc2;
    call_result.gas_left = 200000;
    execute(300000, "605a604160006001f5600155");

    EXPECT_EQ(gas_used, 115817);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);


    ASSERT_EQ(recorded_calls.size(), 1);
    const auto& call_msg = recorded_calls.back();
    EXPECT_EQ(call_msg.create2_salt.bytes[31], 0x5a);
    EXPECT_EQ(call_msg.gas, 263775);
    EXPECT_EQ(call_msg.kind, EVMC_CREATE2);

    auto key = evmc_bytes32{};
    key.bytes[31] = 1;
    EXPECT_EQ(account.storage[key].value.bytes[22], 0xc2);

    EXPECT_EQ(call_msg.input_size, 0x41);
}

TEST_F(evm, create2_salt_cost)
{
    rev = EVMC_CONSTANTINOPLE;
    auto code = "600060208180f5";


    execute(32021, code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(recorded_calls.size(), 1);
    EXPECT_EQ(recorded_calls.back().kind, EVMC_CREATE2);
    EXPECT_EQ(recorded_calls.back().depth, 1);

    execute(32021 - 1, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
    EXPECT_EQ(result.gas_left, 0);
    EXPECT_EQ(recorded_calls.size(), 1);  // No another CREATE2.
}

TEST_F(evm, create_balance_too_low)
{
    rev = EVMC_CONSTANTINOPLE;
    accounts[{}].set_balance(1);
    for (auto op : {OP_CREATE, OP_CREATE2})
    {
        execute(push(2) + (3 * OP_DUP1) + hex(op) + ret_top());
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(std::count(result.output_data, result.output_data + result.output_size, 0), 32);
        EXPECT_EQ(recorded_calls.size(), 0);
    }
}

TEST_F(evm, create_failure)
{
    rev = EVMC_CONSTANTINOPLE;
    for (auto op : {OP_CREATE, OP_CREATE2})
    {
        execute(push(0) + (3 * OP_DUP1) + op + ret_top());
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(std::count(result.output_data, result.output_data + result.output_size, 0), 32);
        ASSERT_GT(recorded_calls.size(), 0);
        EXPECT_EQ(recorded_calls.back().kind, op == OP_CREATE ? EVMC_CREATE : EVMC_CREATE2);
    }
}

TEST_F(evm, call_failing_with_value)
{
    auto code = "60ff600060ff6000600160aa618000f150";

    execute(40000, code);
    EXPECT_GAS_USED(EVMC_SUCCESS, 32447);
    EXPECT_EQ(recorded_calls.size(), 0);  // There was no call().

    execute(0x8000, code);
    EXPECT_STATUS(EVMC_OUT_OF_GAS);
    EXPECT_EQ(recorded_calls.size(), 0);  // There was no call().
}

TEST_F(evm, call_with_value)
{
    auto code = "60ff600060ff6000600160aa618000f150";

    accounts[{}].set_balance(1);
    auto call_dst = evmc_address{};
    call_dst.bytes[19] = 0xaa;
    accounts[call_dst] = {};
    call_result.gas_left = 1;

    execute(40000, code);
    EXPECT_EQ(gas_used, 7447 + 32082);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    ASSERT_EQ(recorded_calls.size(), 1);
    const auto& call_msg = recorded_calls.back();
    EXPECT_EQ(call_msg.kind, EVMC_CALL);
    EXPECT_EQ(call_msg.depth, 1);
    EXPECT_EQ(call_msg.gas, 32083);
    EXPECT_EQ(call_msg.destination, call_dst);
}

TEST_F(evm, call_with_value_depth_limit)
{
    auto call_dst = evmc_address{};
    call_dst.bytes[19] = 0xaa;
    accounts[call_dst] = {};

    msg.depth = 1024;
    execute("60ff600060ff6000600160aa618000f150");
    EXPECT_EQ(gas_used, 7447);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(recorded_calls.size(), 0);
}

TEST_F(evm, call_depth_limit)
{
    rev = EVMC_CONSTANTINOPLE;
    msg.depth = 1024;

    auto code = push(0) + 6 * OP_DUP1;

    for (auto op : {OP_CALL, OP_CALLCODE, OP_DELEGATECALL, OP_STATICCALL, OP_CREATE, OP_CREATE2})
    {
        execute(code + hex(op));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(recorded_calls.size(), 0);
    }
}

TEST_F(evm, call_output)
{
    static bool result_is_correct = false;
    static uint8_t output[] = {0xa, 0xb};

    accounts[{}].set_balance(1);
    call_result.output_data = output;
    call_result.output_size = sizeof(output);
    call_result.release = [](const evmc_result* r) {
        result_is_correct = r->output_size == sizeof(output) && r->output_data == output;
    };

    auto code_prefix_output_1 = push(1) + 6 * OP_DUP1 + push("7fffffffffffffff");
    auto code_prefix_output_0 = push(0) + 6 * OP_DUP1 + push("7fffffffffffffff");
    auto code_suffix = ret(0, 3);

    for (auto op : {OP_CALL, OP_CALLCODE, OP_DELEGATECALL, OP_STATICCALL})
    {
        result_is_correct = false;
        execute(code_prefix_output_1 + hex(op) + code_suffix);
        EXPECT_TRUE(result_is_correct);
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        ASSERT_EQ(result.output_size, 3);
        EXPECT_EQ(result.output_data[0], 0);
        EXPECT_EQ(result.output_data[1], 0xa);
        EXPECT_EQ(result.output_data[2], 0);


        result_is_correct = false;
        execute(code_prefix_output_0 + hex(op) + code_suffix);
        EXPECT_TRUE(result_is_correct);
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        ASSERT_EQ(result.output_size, 3);
        EXPECT_EQ(result.output_data[0], 0);
        EXPECT_EQ(result.output_data[1], 0);
        EXPECT_EQ(result.output_data[2], 0);
    }
}

TEST_F(evm, call_high_gas)
{
    rev = EVMC_HOMESTEAD;
    auto call_dst = evmc_address{};
    call_dst.bytes[19] = 0xaa;
    accounts[call_dst] = {};

    for (auto call_opcode : {"f1", "f2", "f4"})
    {
        execute(5000, 5 * push(0) + push(0xaa) + push(0x134c) + call_opcode);
        EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
    }
}

TEST_F(evm, call_new_account_create)
{
    auto code = "6040600060406000600060aa611770f150";

    call_result.gas_left = 1000;
    execute(9000, code);
    EXPECT_EQ(gas_used, 729 + 5000);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    const auto& call_msg = recorded_calls.back();
    EXPECT_EQ(call_msg.kind, EVMC_CALL);
    EXPECT_EQ(call_msg.depth, 1);
    EXPECT_EQ(call_msg.gas, 6000);
}

TEST_F(evm, callcode_new_account_create)
{
    auto code = "60008080806001600061c350f250";

    accounts[{}].set_balance(1);
    call_result.gas_left = 1;
    execute(100000, code);
    EXPECT_EQ(gas_used, 59722);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    const auto& call_msg = recorded_calls.back();
    EXPECT_EQ(call_msg.kind, EVMC_CALLCODE);
    EXPECT_EQ(call_msg.depth, 1);
    EXPECT_EQ(call_msg.gas, 52300);
}

TEST_F(evm, call_then_oog)
{
    // Performs a CALL then OOG in the same code block.
    auto call_dst = evmc_address{};
    call_dst.bytes[19] = 0xaa;
    accounts[call_dst] = {};
    call_result.status_code = EVMC_FAILURE;
    call_result.gas_left = 0;
    execute(1000, "6040600060406000600060aa60fef180018001800150");
    EXPECT_EQ(gas_used, 1000);
    const auto& call_msg = recorded_calls.back();
    EXPECT_EQ(call_msg.gas, 254);
    EXPECT_EQ(result.gas_left, 0);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm, delegatecall_then_oog)
{
    // Performs a CALL then OOG in the same code block.
    auto call_dst = evmc_address{};
    call_dst.bytes[19] = 0xaa;
    accounts[call_dst] = {};
    call_result.status_code = EVMC_FAILURE;
    call_result.gas_left = 0;
    execute(1000, "604060006040600060aa60fef4800180018001800150");
    EXPECT_EQ(gas_used, 1000);
    const auto& call_msg = recorded_calls.back();
    EXPECT_EQ(call_msg.gas, 254);
    EXPECT_EQ(result.gas_left, 0);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm, staticcall_then_oog)
{
    // Performs a STATICCALL then OOG in the same code block.
    auto call_dst = evmc_address{};
    call_dst.bytes[19] = 0xaa;
    accounts[call_dst] = {};
    call_result.status_code = EVMC_FAILURE;
    call_result.gas_left = 0;
    execute(1000, "604060006040600060aa60fefa800180018001800150");
    EXPECT_EQ(gas_used, 1000);
    const auto& call_msg = recorded_calls.back();
    EXPECT_EQ(call_msg.gas, 254);
    EXPECT_EQ(result.gas_left, 0);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm, staticcall_input)
{
    const auto code = mstore(3, 0x010203) + push(0) + push(0) + push(3) + push(32) + push(0) +
                      push(0xee) + OP_STATICCALL;
    execute(code);
    const auto& call_msg = recorded_calls.back();
    EXPECT_EQ(call_msg.gas, 0xee);
    EXPECT_EQ(call_msg.input_size, 3);
    EXPECT_EQ(to_hex(bytes_view(call_msg.input_data, call_msg.input_size)), "010203");
}

TEST_F(evm, call_with_value_low_gas)
{
    accounts[{}] = {};
    for (auto call_op : {OP_CALL, OP_CALLCODE})
    {
        auto code = 4 * push(0) + push(1) + 2 * push(0) + call_op + OP_POP;
        execute(9721, code);
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(result.gas_left, 2300 - 2);
    }
}

TEST_F(evm, call_oog_after_balance_check)
{
    for (auto op : {OP_CALL, OP_CALLCODE})
    {
        auto code = 4 * push(0) + push(1) + 2 * push(0) + op + OP_SELFDESTRUCT;
        execute(12420, code);
        EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
    }
}

TEST_F(evm, call_oog_after_depth_check)
{
    msg.depth = 1024;
    for (auto op : {OP_CALL, OP_CALLCODE})
    {
        auto code = 4 * push(0) + push(1) + 2 * push(0) + op + OP_SELFDESTRUCT;
        execute(12420, code);
        EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
    }

    rev = EVMC_TANGERINE_WHISTLE;
    auto code = 7 * push(0) + OP_CALL + OP_SELFDESTRUCT;
    execute(25721, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);

    execute(25721 + 5000 - 1, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm, call_wrong_stipend)
{
    rev = EVMC_CONSTANTINOPLE;

    tx_context.block_timestamp = -1;

    const auto orig_code = bytecode{
        "69d145ff031101dd4100414141414144588080804261644a756d70797e03d280d256ca80808080808031084465"
        "7374696e6174696f6e456666666666663f60808008080a665903581c36030a0a0a"};

    const auto code = bytecode{} + OP_PUSH10 + "d145ff031101dd410041" + OP_COINBASE + OP_COINBASE +
                      OP_COINBASE + OP_COINBASE + OP_DIFFICULTY + OP_PC + OP_DUP1 + OP_DUP1 +
                      OP_DUP1 + OP_TIMESTAMP + OP_PUSH2 + "644a" + OP_PUSH22 +
                      "6d70797e03d280d256ca808080808080310844657374" + OP_PUSH10 +
                      "6e6174696f6e45666666" + OP_PUSH7 + "66663f60808008" + OP_ADDMOD + OP_EXP +
                      OP_PUSH7 + "5903581c36030a" + OP_EXP + OP_EXP;

    ASSERT_EQ(code, orig_code);

    execute(247371, code);

    EXPECT_GAS_USED(EVMC_SUCCESS, 1);
}

TEST_F(evm, UB)
{
    const auto code = bytecode{
        "5bc64b0072747469d145ff031101dd4100414141414144588080804261644a756d70797e03d280d256ca808080"
        "808080310844657374696e6174696f6e456666666666663f60808008080a665903581c36030a0a0a0808581b08"
        "6606"};

    execute(code);

    EXPECT_GAS_USED(EVMC_SUCCESS, 1);
}

TEST_F(evm, create_oog_after)
{
    rev = EVMC_CONSTANTINOPLE;
    for (auto op : {OP_CREATE, OP_CREATE2})
    {
        auto code = 4 * push(0) + op + OP_SELFDESTRUCT;
        execute(39000, code);
        EXPECT_STATUS(EVMC_OUT_OF_GAS);
    }
}

TEST_F(evm, returndatasize_before_call)
{
    execute("3d60005360016000f3");
    EXPECT_EQ(gas_used, 17);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 0);
}

TEST_F(evm, returndatasize)
{
    uint8_t output[13];
    call_result.output_size = std::size(output);
    call_result.output_data = std::begin(output);

    auto code = "60008080808080f43d60005360016000f3";
    execute(code);
    EXPECT_EQ(gas_used, 735);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], std::size(output));

    call_result.output_size = 1;
    call_result.status_code = EVMC_FAILURE;
    execute(code);
    EXPECT_EQ(gas_used, 735);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 1);

    call_result.output_size = 0;
    call_result.status_code = EVMC_INTERNAL_ERROR;
    execute(code);
    EXPECT_EQ(gas_used, 735);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 0);
}

TEST_F(evm, returndatacopy)
{
    uint8_t output[32] = {1, 2, 3, 4, 5, 6, 7};
    call_result.output_size = std::size(output);
    call_result.output_data = std::begin(output);

    auto code = "600080808060aa60fff4506020600060003e60206000f3";
    execute(code);
    EXPECT_EQ(gas_used, 999);
    ASSERT_EQ(result.output_size, 32);
    EXPECT_EQ(result.output_data[0], 1);
    EXPECT_EQ(result.output_data[1], 2);
    EXPECT_EQ(result.output_data[2], 3);
    EXPECT_EQ(result.output_data[6], 7);
    EXPECT_EQ(result.output_data[7], 0);
}

TEST_F(evm, returndatacopy_empty)
{
    auto code = "600080808060aa60fff4600080803e60016000f3";
    execute(code);
    EXPECT_EQ(gas_used, 994);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 0);
}

TEST_F(evm, returndatacopy_cost)
{
    auto output = uint8_t{};
    call_result.output_data = &output;
    call_result.output_size = sizeof(output);
    auto code = "60008080808080fa6001600060003e";
    execute(736, code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    execute(735, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm, returndatacopy_outofrange)
{
    auto output = uint8_t{};
    call_result.output_data = &output;
    call_result.output_size = sizeof(output);
    execute(735, "60008080808080fa6002600060003e");
    EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);

    execute(735, "60008080808080fa6001600160003e");
    EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);

    execute(735, "60008080808080fa6000600260003e");
    EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);
}
