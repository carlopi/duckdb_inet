#define DUCKDB_EXTENSION_MAIN

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/pair.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_type_info.hpp"
#include "duckdb/common/operator/cast_operators.hpp"
#include "duckdb/function/cast/cast_function_set.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/main/config.hpp"
#include "inet-extension.hpp"

namespace duckdb {

struct IPAddress {
	hugeint_t address;
	int32_t mask;
};

static bool IPAddressError(string_t input, string *error_message, string error) {
	string e = "Failed to convert string \"" + input.GetString() + "\" to inet: " + error;
	HandleCastError::AssignError(e, error_message);
	return false;
}

static bool TryParseIPAddress(string_t input, IPAddress &result, string *error_message) {
	auto data = input.GetDataUnsafe();
	auto size = input.GetSize();
	idx_t c = 0;
	idx_t number_count = 0;
	int32_t address = 0;
parse_number:
	idx_t start = c;
	while (c < size && data[c] >= '0' && data[c] <= '9') {
		c++;
	}
	if (start == c) {
		return IPAddressError(input, error_message, "Expected a number");
	}
	uint8_t number;
	if (!TryCast::Operation<string_t, uint8_t>(string_t(data + start, c - start), number)) {
		return IPAddressError(input, error_message, "Expected a number between 0 and 255");
	}
	address += number << (8 * number_count);
	number_count++;
	result.address = address;
	if (number_count == 4) {
		goto parse_mask;
	} else {
		goto parse_dot;
	}
parse_dot:
	if (c == size || data[c] != '.') {
		return IPAddressError(input, error_message, "Expected a dot");
	}
	c++;
	goto parse_number;
parse_mask:
	if (c == size) {
		// no mask, default to 32
		result.mask = 32;
		return true;
	}
	if (data[c] != '/') {
		return IPAddressError(input, error_message, "Expected a slash");
	}
	uint8_t mask;
	if (!TryCast::Operation<string_t, uint8_t>(string_t(data + start, c - start), mask)) {
		return IPAddressError(input, error_message, "Expected a number between 0 and 255");
	}
	result.mask = mask;
	return true;
}

static bool VarcharToINETCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
	UnifiedVectorFormat vdata;
	source.ToUnifiedFormat(count, vdata);

	auto &entries = StructVector::GetEntries(result);
	auto address_data = FlatVector::GetData<hugeint_t>(*entries[0]);
	auto mask_data = FlatVector::GetData<int32_t>(*entries[1]);

	auto input = (string_t *)vdata.data;
	for (idx_t i = 0; i < count; i++) {
		auto idx = vdata.sel->get_index(i);

		if (!vdata.validity.RowIsValid(idx)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}
		IPAddress inet;
		if (!TryParseIPAddress(input[idx], inet, parameters.error_message)) {
			return false;
		}
		address_data[i] = inet.address;
		mask_data[i] = inet.mask;
	}
	return true;
}

static bool INETToVarcharCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
	throw InternalException("INET to Varchar cast");
}

void INETExtension::Load(DuckDB &db) {
	Connection con(db);
	con.BeginTransaction();

	auto &catalog = Catalog::GetCatalog(*con.context);

	// add the "inet" type
	child_list_t<LogicalType> children;
	children.push_back(make_pair("address", LogicalType::HUGEINT));
	children.push_back(make_pair("mask", LogicalType::USMALLINT));
	auto inet_type = LogicalType::STRUCT(move(children));
	inet_type.SetAlias("inet");

	CreateTypeInfo info("inet", inet_type);
	catalog.CreateType(*con.context, &info);

	// add inet casts
	auto &config = DBConfig::GetConfig(*con.context);

	auto &casts = config.GetCastFunctions();
	casts.RegisterCastFunction(LogicalType::VARCHAR, inet_type, VarcharToINETCast);
	casts.RegisterCastFunction(inet_type, LogicalType::VARCHAR, INETToVarcharCast);

	con.Commit();
}

std::string INETExtension::Name() {
	return "inet";
}

} // namespace duckdb

extern "C" {

DUCKDB_EXTENSION_API void inet_init(duckdb::DatabaseInstance &db) {
	duckdb::DuckDB db_wrapper(db);
	db_wrapper.LoadExtension<duckdb::INETExtension>();
}

DUCKDB_EXTENSION_API const char *inet_version() {
	return duckdb::DuckDB::LibraryVersion();
}
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif