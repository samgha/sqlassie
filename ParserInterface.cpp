/*
 * SQLassie - database firewall
 * Copyright (C) 2011 Brandon Skari <brandon.skari@gmail.com>
 * 
 * This file is part of SQLassie.
 *
 * SQLassie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * SQLassie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with SQLassie. If not, see <http://www.gnu.org/licenses/>.
 */

#include "clearStack.hpp"
#include "nullptr.hpp"
#include "parser.tab.hpp"
#include "ParserInterface.hpp"
#include "scanner.yy.hpp"

#include <boost/thread/mutex.hpp>
#include <cassert>
#include <exception>
#include <stack>
#include <string>

using boost::lock_guard;
using boost::mutex;
using std::bad_alloc;
using std::size_t;
using std::stack;
using std::string;

// Methods from the parser
extern int yyparse(QueryRisk* const qrPtr, ParserInterface* const pi);

// Static members
mutex ParserInterface::parserMutex_;


/**
 * Hide some scanner members behind a PIMPL so that I don't have to worry
 * about dependency issues in my Makefile. Long explanation: I'm using a
 * script to update my Makefile dependencies. I used to include
 * scanner.yy.hpp, the header file generated by Flex, in ParserInterface's
 * header file. However, my script only looks at cpp dependencies because
 * it assumes that header files are static and not dynamically generated,
 * which works just fine for the other 99% of my files but doesn't work for
 * Flex. Anyway, I'm just going to hide the scanner's members here so that I
 * can break that nasty dependency.
 */
class ParserInterfaceScannerMembers
{
public:
	ParserInterfaceScannerMembers(const char* const query);
	~ParserInterfaceScannerMembers();
	yyscan_t scanner_;
	YY_BUFFER_STATE bufferState_;
private:
	ParserInterfaceScannerMembers(const ParserInterfaceScannerMembers&);
	ParserInterfaceScannerMembers& operator=(const ParserInterfaceScannerMembers&);
};


ParserInterface::ParserInterface(const string& buffer) :
	scannerContext_(),
	parsed_(false),
	qr_(),
	parserStatus_(0),
	bufferLen_(buffer.size()),
	tokensHash_(),
	scannerPimpl_(new ParserInterfaceScannerMembers(buffer.c_str()))
{
}


ParserInterface::~ParserInterface()
{
	delete scannerPimpl_;
}


int ParserInterface::parse(QueryRisk* const qrPtr)
{
	assert(NULL != qrPtr);
	if (parsed_)
	{
		*qrPtr = qr_;
		return parserStatus_;
	}

	int parserStatus;
	// Start a new block so I can do a scoped, exception safe lock
	{
		/// @TODO ticket #3 make Bison reentrant so I don't have to use a big lock
		lock_guard<mutex> m(parserMutex_);
	
		// Clear the stacks before every parsing attempt
		clearStack(&scannerContext_.identifiers);
		clearStack(&scannerContext_.quotedStrings);
		clearStack(&scannerContext_.numbers);
		
		parserStatus = yyparse(qrPtr, this);

		#ifndef NDEBUG
			if (0 == parserStatus && qrPtr->valid)
			{
				assert(scannerContext_.identifiers.empty() && "Identifiers stack not empty");
				assert(scannerContext_.quotedStrings.empty() && "Quoted strings stack not empty");
				assert(scannerContext_.numbers.empty() && "Numbers stack not empty");
			}
		#endif
		
	} // Unlock the mutex
	
	qr_ = *qrPtr;
	parserStatus_ = parserStatus;
	parsed_ = true;

	// If the parser failed, we still need to manually calculate the rest of
	// the hash for this query. That calculation is handled in yylex, so just
	// keep calling yylex ourselves until it hits the end of the buffer.
	if (parserStatus != 0)
	{
		const int MIN_VALID_TOKEN = 255;
		while(yylex(NULL, qrPtr, this) > MIN_VALID_TOKEN);
	}
	
	return parserStatus;
}


ParserInterface::QueryHash ParserInterface::getHash() const
{
	assert(parsed_ && "gethash() called before parse(QueryRisk* const)");
	return tokensHash_;
}


ParserInterface::QueryHash::QueryHash() :
	hash(0),
	tokensCount(0)
{
}


bool operator==(
	const ParserInterface::QueryHash& hash1,
	const ParserInterface::QueryHash& hash2
)
{
	return hash1.hash == hash2.hash
		&& hash1.tokensCount == hash2.tokensCount;
}


size_t hash_value(const ParserInterface::QueryHash& qh)
{
	return static_cast<std::size_t>(qh.hash + qh.tokensCount);
}


ParserInterfaceScannerMembers::ParserInterfaceScannerMembers(
	const char* const query
) :
	scanner_(),
	bufferState_(nullptr)
{
	if (0 != sql_lex_init(&scanner_))
	{
		throw bad_alloc();
	}
	bufferState_ = sql__scan_string(query, scanner_);
	if (nullptr == bufferState_)
	{
		sql_lex_destroy(scanner_);
		throw bad_alloc();
	}
}


ParserInterfaceScannerMembers::~ParserInterfaceScannerMembers()
{
	sql__delete_buffer(bufferState_, scanner_);
	sql_lex_destroy(scanner_);
}


/**
 * Customized yylex so that we can fool the parser into not calling the real
 * sql_lex directly. This way, we can keep track of the tokens from the query
 * and get all of the tokens even if parsing fails, so that we can do things
 * like whitelist queries. This is a friend of ParserInterface.
 * @param qr The QueryRisk attributes of the parsed query.
 * @param pi ParserInterface reference so that we can call the real sql_lex
 * and so that we can store the hash of the query's tokens.
 */
int yylex(YYSTYPE* lvalp, QueryRisk* const qr, ParserInterface* const pi);

/**
 * Calculates the partial sdbm hash, given a new lexeme.
 * @param lexCode The new lexeme from the buffer stream.
 * @param ht The previous hash.
 */
static ParserInterface::hashType sdbmHash(
	const int lexCode,
	const ParserInterface::hashType ht
);


extern int sql_lex(YYSTYPE* lvalp, QueryRisk* const qrPtr, yyscan_t const scanner);

int yylex(YYSTYPE* lvalp, QueryRisk* const qr, ParserInterface* const pi)
{
	assert(nullptr != qr);
	assert(nullptr != pi);

	int lexCode = sql_lex(lvalp, qr, pi->scannerPimpl_->scanner_);
	// Don't calculate the hash anymore once we've hit the end of the buffer
	if (lexCode > 255)
	{
		++pi->tokensHash_.tokensCount;
		pi->tokensHash_.hash = sdbmHash(lexCode, pi->tokensHash_.hash);
	}
	return lexCode;
}


static ParserInterface::hashType sdbmHash(
	const int lexCode,
	const ParserInterface::hashType ht
)
{
	return lexCode + (ht << 6) + (ht << 16) - ht;
}
