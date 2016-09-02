// -*- c-basic-offset: 4 -*-
/*
 *  This file is part of the KDE libraries
 *  Copyright (C) 1999-2001 Harri Porten (porten@kde.org)
 *  Copyright (C) 2001 Peter Kelly (pmk@post.com)
 *  Copyright (C) 2003, 2006, 2007 Apple Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"
#include "Parser.h"

#include "lexer.h"
#include <wtf/HashSet.h>
#include <wtf/Vector.h>

extern int kjsyyparse();

namespace KJS {

void Parser::parse(int* errLine, UString* errMsg)
{
    ASSERT(!m_sourceElements);

    if (errLine)
        *errLine = -1;
    if (errMsg)
        *errMsg = 0;
        
    Lexer& lexer = KJS::lexer();
    lexer.setCode(*m_source);

    int parseError = kjsyyparse();
    bool lexError = lexer.sawError();
    lexer.clear();

    ParserRefCounted::deleteNewObjects();

    if (parseError || lexError) {
        if (errLine)
            *errLine = lexer.lineNo();
        if (errMsg)
            *errMsg = "Parse error";
        m_sourceElements.clear();
    }
}

void Parser::reparse(FunctionBodyNode* functionBodyNode)
{
    m_source = &functionBodyNode->source();
    parse(0, 0);
    ASSERT(m_sourceElements);

    functionBodyNode->setData(m_sourceElements.get(),
                              m_varDeclarations ? &m_varDeclarations->data : 0, 
                              m_funcDeclarations ? &m_funcDeclarations->data : 0);
    functionBodyNode->setLoc(m_source->firstLine(), m_lastLine);

    m_source = 0;
    m_sourceElements = 0;
    m_varDeclarations = 0;
    m_funcDeclarations = 0;
}

void Parser::didFinishParsing(SourceElements* sourceElements, ParserRefCountedData<DeclarationStacks::VarStack>* varStack, 
                              ParserRefCountedData<DeclarationStacks::FunctionStack>* funcStack, int lastLine)
{
    m_sourceElements = sourceElements ? sourceElements : new SourceElements;
    m_varDeclarations = varStack;
    m_funcDeclarations = funcStack;
    m_lastLine = lastLine;
}

Parser& parser()
{
    ASSERT(JSLock::currentThreadIsHoldingLock());

    static Parser& staticParser = *new Parser;
    return staticParser;
}

} // namespace KJS
