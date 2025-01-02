#include "pqxx_cp.h"

namespace cp {
	std::string serialize(pqxx::result res) {
		// TODO: it can be better
		if (res.empty()) {
			return "{\"result\": []}";
		}
		std::string res_str = "{\"result\": [";
		for (auto const &row: res) {
			res_str += '{';
			for (auto const &field: row) {
				res_str += '"' + std::string(field.name()) + "\": \"" + std::string(field.c_str()) + "\",";
			}
			res_str[res_str.length() - 1] = '}';
			res_str += ',';
		}
		res_str[res_str.length() - 1] = ']';
		res_str += "}";
		return res_str;
	}

	std::string serialize(pqxx::row row) {
		// TODO: it can be better
		if (row.empty()) {
			return "{\"result\": []}";
		}
		std::string res_str = "{\"result\": [";
		res_str += '{';
		for (auto const &field: row) {
			res_str += '"' + std::string(field.name()) + "\": \"" + std::string(field.c_str()) + "\",";
		}
		res_str[res_str.length() - 1] = '}';
		res_str += ',';
		
		res_str[res_str.length() - 1] = ']';
		res_str += "}";
		return res_str;
	}

	connection_manager::connection_manager(std::unique_ptr<pqxx::connection>& connection) : connection(std::move(connection)) {};

	void connection_manager::prepare(const std::string& name, const std::string& definition) {
		std::scoped_lock lock(prepares_mutex);
		if (prepares.contains(name))
			return;

		connection->prepare(name, definition);
		prepares.insert(name);
	}

	connection_pool::connection_pool(const connection_options& options) {
			for (int i = 0; i < options.connections_count; ++i) {
				const auto connect_string = std::format("dbname = {} user = {} password = {} hostaddr = {} port = {}", options.dbname, options.user, options.password, options.hostaddr, options.port);
				try {
					auto connection = std::make_unique<pqxx::connection>(connect_string);
					auto manager = std::make_unique<connection_manager>(connection);
					connections.push(std::move(manager));
				} catch(...) {
					throw ("Connection failed. Check your internet and sql config");
				}
			}
	}

	void connection_pool::return_connection(std::unique_ptr<connection_manager>& manager) {
			// return the borrowed connection
			{
				std::scoped_lock lock(connections_mutex);
				connections.push(std::move(manager));
			}

			// notify that we're done
			connections_cond.notify_one();
		}

	std::unique_ptr<connection_manager> connection_pool::borrow_connection() {
			std::unique_lock lock(connections_mutex);
			connections_cond.wait(lock, [this]() { return !connections.empty(); });

			// if we have something here, we can borrow it from the queue
			auto manager = std::move(connections.front());
			connections.pop();
			return manager;
	}


	basic_connection::basic_connection(connection_pool& pool) : pool(pool) {
		manager = pool.borrow_connection();
	}

	basic_connection::~basic_connection() {
		pool.return_connection(manager);
	}

	pqxx::connection& basic_connection::get() const { return *manager->connection; }

	basic_connection::operator pqxx::connection&() { return get(); }
	basic_connection::operator const pqxx::connection&() const { return get(); }

	pqxx::connection* basic_connection::operator->() { return manager->connection.get(); }
	const pqxx::connection* basic_connection::operator->() const { return manager->connection.get(); }

	void basic_connection::prepare(std::string_view name, std::string_view definition) {
		manager->prepare(std::string(name), std::string(definition));
	}


	query_manager::query_manager(basic_transaction& transaction, std::string_view query_id) : transaction_view(transaction), query_id(query_id) {}
	
	query::query(std::string_view str) : str(str) {}

	const char* query::data() const {
			return str.data();
		}

		query::operator std::string() const {
			return { str.begin(), str.end() };
		}

		constexpr query::operator std::string_view() const {
			return { str.data(), str.size() };
		}


	struct named_query : query {
		named_query(std::string_view name, std::string_view str) : query(str), name(name) {}

		friend struct query_manager;
		friend struct basic_transaction;

	protected:
		std::string name;
	};

	void basic_transaction::prepare_one(const query& q) {
			const auto query_id = std::format("{:X}", std::hash<std::string_view>()(q));
			connection.prepare(query_id, q);
			q.manager.emplace(*this, query_id);
	}
	
	void basic_transaction::prepare_one(const named_query& q) {
			connection.prepare(q.name, q);
			q.manager.emplace(*this, q.name);
	}
	
	pqxx::result basic_transaction::exec(std::string_view q) { return transaction.exec(q); }
	
	void basic_transaction::commit() { transaction.commit(); }
	
	void basic_transaction::abort() { transaction.abort(); }

	pqxx::work& basic_transaction::get() { return transaction; }
	
	basic_transaction::operator pqxx::work&() { return get(); }
}