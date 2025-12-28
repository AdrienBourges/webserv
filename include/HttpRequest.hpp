/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpRequest.hpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: you <you@student.42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/11/28 by you                       #+#    #+#             */
/*   Updated: 2025/11/29 by you                       ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTPREQUEST_HPP
# define HTTPREQUEST_HPP

# include <string>
# include <map>

/*
    HttpRequest

    Représente une requête HTTP déjà parsée.

    On gère :
      - la ligne de requête : METHOD SP TARGET SP HTTP-VERSION
      - les headers (clé:valeur)
      - le body (rempli par le serveur après la lecture complète)

    Le body n'est PAS parsé par la méthode parse() : celle-ci ne s'occupe
    que de la ligne de requête + des headers. Le WebServer se charge de
    lire le body en fonction de Content-Length, puis appelle setBody().
*/

class HttpRequest
{
public:
	HttpRequest();
	~HttpRequest();

	// Parse la partie "start-line + headers" (sans le body).
	bool parse(const std::string &raw);

	const std::string &getMethod() const;
	const std::string &getTarget() const;
	const std::string &getVersion() const;

	const std::map<std::string, std::string> &getHeaders() const;
	bool hasHeader(const std::string &name) const;
	std::string getHeader(const std::string &name) const;

	// Body (fixé par le WebServer après lecture complète)
	void setBody(const std::string &body);
	const std::string &getBody() const;

private:
	std::string _method;
	std::string _target;
	std::string _version;
	std::map<std::string, std::string> _headers;
	std::string _body;
};

#endif // HTTPREQUEST_HPP

